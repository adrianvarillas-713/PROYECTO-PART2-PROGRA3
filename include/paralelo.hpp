#pragma once

#include <string>
#include <vector>

#include "buscador.hpp"
#include "catalogo.hpp"
#include "trie.hpp"

namespace pf {

    struct ResultadoBenchmark {
        unsigned int numHilos = 1;
        size_t stringsProcesados = 0;
        double msInsercionSecuencial = 0.0;
        double msInsercionParalela = 0.0;
        double msBusquedaSecuencial = 0.0;
        double msBusquedaParalela = 0.0;
    };

    class MotorParalelo {
    public:
        // Optimiza la normalizacion, tokenizacion e insercion de strings en un Trie
        // usando N hilos en paralelo (Map-Reduce).
        static size_t insertarStringsParalelo(const Catalogo &catalogo,
                                              TriePrefijos &trieDestino,
                                              unsigned int numHilos = 0);

        // Optimiza la busqueda de frases exactas sobre sinopsis en paralelo.
        static void reordenarPorFraseParalelo(const Catalogo &catalogo,
                                              const std::string &fraseNormalizada,
                                              std::vector<Resultado> &resultados,
                                              unsigned int numHilos = 0);

        // Ejecuta multiples consultas de texto de forma concurrente.
        static std::vector<std::vector<Resultado>>
        buscarLoteParalelo(const Buscador &buscador,
                           const std::vector<std::string> &consultas,
                           size_t limitePorConsulta = 20);

        // Compara tiempos reales (1 hilo vs N hilos) para demostrar la mejora.
        static ResultadoBenchmark ejecutarBenchmark(const Catalogo &catalogo,
                                                    const Buscador &buscador);
    };

} // namespace pf