#include "paralelo.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>
#include <unordered_set>

#include "normalizador.hpp"

namespace pf {

size_t MotorParalelo::insertarStringsParalelo(const Catalogo &catalogo,
                                              TriePrefijos &trieDestino,
                                              unsigned int numHilos) {
  const size_t total = catalogo.tamano();
  if (total == 0) return 0;

  if (numHilos == 0) {
    numHilos = std::max(1u, std::thread::hardware_concurrency());
  }

  // Fase MAP: cada hilo normaliza y extrae tokens unicos de su bloque sin locks
  std::vector<std::vector<std::string>> tokensPorHilo(numHilos);
  std::vector<std::thread> trabajadores;
  trabajadores.reserve(numHilos);

  const size_t bloque = (total + numHilos - 1) / numHilos;

  for (unsigned int h = 0; h < numHilos; ++h) {
    const size_t inicio = h * bloque;
    const size_t fin = std::min(inicio + bloque, total);
    if (inicio >= fin) break;

    trabajadores.emplace_back([&, h, inicio, fin]() {
      std::unordered_set<std::string> locales;
      locales.reserve(16384);
      std::vector<std::string> tokens;
      tokens.reserve(256);

      for (size_t d = inicio; d < fin; ++d) {
        const Pelicula &p = catalogo[d];
        tokens.clear();
        tokenizarNormalizado(normalizar(p.titulo), tokens);
        for (const auto &t : tokens) {
          if (t.size() <= 30) locales.insert(t);
        }
        tokens.clear();
        tokenizarNormalizado(normalizar(p.sinopsis), tokens);
        for (const auto &t : tokens) {
          if (t.size() <= 30) locales.insert(t);
        }
      }

      tokensPorHilo[h].assign(locales.begin(), locales.end());
    });
  }

  for (auto &t : trabajadores) t.join();

  // Fase REDUCE: insercion consolidada en el TriePrefijos
  size_t totalStrings = 0;
  for (const auto &lista : tokensPorHilo) {
    for (const auto &palabra : lista) {
      trieDestino.insertar(palabra);
      ++totalStrings;
    }
  }
  return totalStrings;
}

void MotorParalelo::reordenarPorFraseParalelo(
    const Catalogo &catalogo, const std::string &fraseNormalizada,
    std::vector<Resultado> &resultados, unsigned int numHilos) {
  if (fraseNormalizada.size() < 3 || resultados.empty()) return;

  if (numHilos == 0) {
    numHilos = std::max(1u, std::thread::hardware_concurrency());
  }

  const size_t total = resultados.size();
  const size_t bloque = (total + numHilos - 1) / numHilos;
  std::vector<std::future<void>> tareas;

  for (unsigned int h = 0; h < numHilos; ++h) {
    const size_t inicio = h * bloque;
    const size_t fin = std::min(inicio + bloque, total);
    if (inicio >= fin) break;

    tareas.push_back(std::async(std::launch::async, [&, inicio, fin]() {
      for (size_t i = inicio; i < fin; ++i) {
        const Pelicula &p = catalogo[resultados[i].doc];
        if (p.tituloNorm.find(fraseNormalizada) != std::string::npos) {
          resultados[i].puntaje += 12.0;
        } else if (normalizar(p.sinopsis).find(fraseNormalizada) !=
                   std::string::npos) {
          resultados[i].puntaje += 3.5;
        }
      }
    }));
  }

  for (auto &f : tareas) f.get();
}

std::vector<std::vector<Resultado>>
MotorParalelo::buscarLoteParalelo(const Buscador &buscador,
                                  const std::vector<std::string> &consultas,
                                  size_t limitePorConsulta) {
  std::vector<std::future<std::vector<Resultado>>> futuros;
  futuros.reserve(consultas.size());

  for (const auto &qStr : consultas) {
    futuros.push_back(std::async(std::launch::async, [&, qStr]() {
      Consulta q = buscador.interpretar(qStr);
      if (!q.valida) return std::vector<Resultado>{};
      return buscador.buscar(q, limitePorConsulta);
    }));
  }

  std::vector<std::vector<Resultado>> salida;
  salida.reserve(consultas.size());
  for (auto &f : futuros) {
    salida.push_back(f.get());
  }
  return salida;
}

ResultadoBenchmark MotorParalelo::ejecutarBenchmark(const Catalogo &catalogo,
                                                    const Buscador &buscador) {
  using Reloj = std::chrono::steady_clock;
  ResultadoBenchmark bench;
  bench.numHilos = std::max(1u, std::thread::hardware_concurrency());

  // 1. Insercion de strings: 1 hilo vs N hilos
  {
    TriePrefijos trieSec;
    auto t0 = Reloj::now();
    bench.stringsProcesados = insertarStringsParalelo(catalogo, trieSec, 1);
    bench.msInsercionSecuencial =
        std::chrono::duration<double, std::milli>(Reloj::now() - t0).count();
  }
  {
    TriePrefijos triePar;
    auto t0 = Reloj::now();
    insertarStringsParalelo(catalogo, triePar, bench.numHilos);
    bench.msInsercionParalela =
        std::chrono::duration<double, std::milli>(Reloj::now() - t0).count();
  }

  // 2. Busqueda de strings (lote de 16 consultas): secuencial vs paralelo
  const std::vector<std::string> consultasPrueba = {
      "ghost ship",   "space adventure", "murder mystery",  "love story",
      "world war",    "time travel",     "secret agent",    "high school",
      "bank robbery", "alien invasion",  "family drama",    "serial killer",
      "lost island",  "treasure hunt",   "musical comedy",  "courtroom trial"};

  {
    auto t0 = Reloj::now();
    for (const auto &c : consultasPrueba) {
      Consulta q = buscador.interpretar(c);
      auto res = buscador.buscar(q, 50);
      reordenarPorFraseParalelo(catalogo, normalizar(c), res, 1);
    }
    bench.msBusquedaSecuencial =
        std::chrono::duration<double, std::milli>(Reloj::now() - t0).count();
  }
  {
    auto t0 = Reloj::now();
    auto lote = buscarLoteParalelo(buscador, consultasPrueba, 50);
    for (size_t i = 0; i < lote.size(); ++i) {
      reordenarPorFraseParalelo(catalogo, normalizar(consultasPrueba[i]),
                                lote[i], bench.numHilos);
    }
    bench.msBusquedaParalela =
        std::chrono::duration<double, std::milli>(Reloj::now() - t0).count();
  }

  return bench;
}

} // namespace pf