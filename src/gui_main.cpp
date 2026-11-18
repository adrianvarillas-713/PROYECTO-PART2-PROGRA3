#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include "normalizador.hpp"
#include "paralelo.hpp"
#include "patrones.hpp"

namespace {

std::string unirLista(const std::vector<std::string> &v,
                      const std::string &sep = ", ", size_t maxItems = 8) {
  std::string out;
  for (size_t i = 0; i < v.size() && i < maxItems; ++i) {
    if (i > 0) out += sep;
    out += v[i];
  }
  if (v.size() > maxItems) {
    out += sep + "(+" + std::to_string(v.size() - maxItems) + ")";
  }
  return out;
}

} // namespace

int main() {
  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  GLFWwindow *window = glfwCreateWindow(
      1320, 840, "UTEC Stream - CS2013 Programacion III", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 330");

  // 1. PATRON SINGLETON: Inicializar el unico motor de la aplicacion
  pf::MotorStream *motor = pf::MotorStream::getInstance();
  const bool csvCargado =
      motor->inicializar("data/wiki_movie_plots_deduped.csv", true);

  // Estado persistente del usuario + PATRON MEMENTO
  const std::string rutaEstado = "datos_usuario.txt";
  pf::EstadoUsuario estadoUsuario;
  if (csvCargado) {
    estadoUsuario.cargar(rutaEstado, motor->catalogo());
  }
  pf::GestorEstadoUsuario gestorEstado(estadoUsuario);
  pf::HistorialUsuario historialMemento;

  // Variables de estado de la interfaz
  char bufTexto[256] = "";
  char bufDirector[128] = "";
  char bufGenero[128] = "";
  char bufOrigen[128] = "";
  int filtroAnio = 0;

  std::vector<pf::Resultado> resultados;
  std::vector<std::string> sugerenciasTrie;
  std::string tituloLista = "RECOMENDADAS PARA TI";
  double tiempoBusquedaMs = 0.0;
  int docSeleccionado = -1;

  // Variables para el PATRON PROXY
  bool cuentaActiva = true;
  bool regionPermitida = true;
  std::string mensajeProxy;

  // Variables para el ARBOL AVL
  int rangoAvl[2] = {1990, 2020};

  // Variables para PROGRAMACION PARALELA
  pf::ResultadoBenchmark ultimoBench;
  bool hayBenchmark = false;

  // Lambda que usa PATRON BUILDER + BUSQUEDA PARALELA
  auto ejecutarBusquedaBuilder = [&]() {
    if (!csvCargado) return;
    auto t0 = std::chrono::steady_clock::now();

    pf::ConsultaBuilder builder;
    if (bufTexto[0] != '\0') builder.texto(bufTexto);
    if (bufDirector[0] != '\0') builder.director(bufDirector);
    if (bufGenero[0] != '\0') builder.genero(bufGenero);
    if (bufOrigen[0] != '\0') builder.origen(bufOrigen);
    if (filtroAnio > 0) builder.anio(filtroAnio);

    pf::Consulta q = builder.build(motor->buscador());
    if (q.valida) {
      resultados = motor->buscador().buscar(q, 100);
      if (!q.textoNormalizado.empty()) {
        pf::MotorParalelo::reordenarPorFraseParalelo(
            motor->catalogo(), q.textoNormalizado, resultados);
      }
      tituloLista = "RESULTADOS DE BUSQUEDA (" +
                    std::to_string(resultados.size()) + ")";
    } else {
      resultados.clear();
      tituloLista = q.mensaje;
    }

    tiempoBusquedaMs = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - t0)
                           .count();
  };

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("UTEC_Stream_Main", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove);

    if (!csvCargado) {
      ImGui::TextColored(
          ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
          "ERROR: No se encontro 'data/wiki_movie_plots_deduped.csv'.");
      ImGui::TextWrapped(
          "Asegurate de colocar la carpeta 'data/' con el CSV junto al "
          "ejecutable o en la raiz del proyecto.");
      ImGui::End();
      ImGui::Render();
      int w, h;
      glfwGetFramebufferSize(window, &w, &h);
      glViewport(0, 0, w, h);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
      glfwSwapBuffers(window);
      continue;
    }

    // ========================================================================
    // CABECERA + PATRON MEMENTO (UNDO)
    // ========================================================================
    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f),
                       "UTEC STREAM (Singleton Activo)");
    ImGui::SameLine();
    ImGui::Text("| %zu peliculas | %zu palabras en Trie | Altura AVL: %d",
                motor->catalogo().tamano(),
                motor->indice().vocabulario().numTerminos(),
                motor->arbolAnios().alturaTotal());

    ImGui::SameLine(ImGui::GetWindowWidth() - 260);
    ImGui::BeginDisabled(!historialMemento.hayHistorial());
    if (ImGui::Button("Deshacer (Memento)")) {
      estadoUsuario.cargar(rutaEstado, motor->catalogo());
      gestorEstado.restaurarEstado(historialMemento.getEstadoAnterior());
      estadoUsuario.guardar(rutaEstado, motor->catalogo());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu)", historialMemento.cantidad());

    ImGui::Separator();

    // ========================================================================
    // PESTAÑAS DE FUNCIONALIDAD
    // ========================================================================
    if (ImGui::BeginTabBar("TabsPrincipales")) {
      // ----------------------------------------------------------------------
      // TAB 1: BUSCADOR (Builder + Trie Autocomplete + Proxy + Memento)
      // ----------------------------------------------------------------------
      if (ImGui::BeginTabTabItem("Explorar y Buscar") ||
          ImGui::BeginTabItem("Explorar y Buscar")) {
        // Fila 1: Busqueda principal + Autocompletado TriePrefijos
        ImGui::SetNextItemWidth(380);
        if (ImGui::InputTextWithHint("##txt", "Palabra, frase o sub-palabra...",
                                     bufTexto, sizeof(bufTexto),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
          ejecutarBusquedaBuilder();
          sugerenciasTrie.clear();
        }
        if (ImGui::IsItemEdited()) {
          std::string norm = pf::recortar(pf::normalizar(bufTexto));
          size_t sp = norm.find_last_of(' ');
          std::string prefijo =
              (sp == std::string::npos) ? norm : norm.substr(sp + 1);
          sugerenciasTrie.clear();
          if (prefijo.size() >= 2) {
            auto ids = motor->indice().vocabulario().conPrefijo(prefijo, 6);
            for (int id : ids) {
              sugerenciasTrie.push_back(
                  motor->indice().vocabulario().termino(id));
            }
          }
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(160);
        ImGui::InputTextWithHint("##dir", "Director...", bufDirector,
                                 sizeof(bufDirector));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        ImGui::InputTextWithHint("##gen", "Genero...", bufGenero,
                                 sizeof(bufGenero));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130);
        ImGui::InputTextWithHint("##ori", "Origen...", bufOrigen,
                                 sizeof(bufOrigen));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(95);
        ImGui::InputInt("Anio", &filtroAnio, 0);
        if (filtroAnio < 0) filtroAnio = 0;

        ImGui::SameLine();
        if (ImGui::Button("Buscar (Builder)")) {
          ejecutarBusquedaBuilder();
          sugerenciasTrie.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Limpiar")) {
          bufTexto[0] = bufDirector[0] = bufGenero[0] = bufOrigen[0] = '\0';
          filtroAnio = 0;
          resultados.clear();
          sugerenciasTrie.clear();
          tituloLista = "RECOMENDADAS PARA TI";
        }

        // Chips de sugerencias del TriePrefijos
        if (!sugerenciasTrie.empty()) {
          ImGui::TextDisabled("Autocompletar (TriePrefijos):");
          for (const auto &sug : sugerenciasTrie) {
            ImGui::SameLine();
            if (ImGui::SmallButton(sug.c_str())) {
              std::strncpy(bufTexto, sug.c_str(), sizeof(bufTexto) - 1);
              ejecutarBusquedaBuilder();
              sugerenciasTrie.clear();
              break;
            }
          }
        }

        // Filtro rapido por rango usando el ARBOL AVL
        ImGui::SetNextItemWidth(240);
        ImGui::SliderInt2("Rango de Anios (Arbol AVL)", rangoAvl, 1900, 2026);
        ImGui::SameLine();
        if (ImGui::Button("Filtrar con AVL")) {
          auto t0 = std::chrono::steady_clock::now();
          auto docs = motor->arbolAnios().buscarPorRango(rangoAvl[0], rangoAvl[1]);
          resultados.clear();
          for (size_t i = 0; i < docs.size() && i < 150; ++i) {
            resultados.push_back({docs[i], 1.0, "encontrado en Arbol AVL"});
          }
          tiempoBusquedaMs = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - t0)
                                 .count();
          tituloLista = "PELICULAS ENTRE " + std::to_string(rangoAvl[0]) +
                        " Y " + std::to_string(rangoAvl[1]) + " (" +
                        std::to_string(docs.size()) + " en AVL)";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("| Tiempo: %.2f ms", tiempoBusquedaMs);

        ImGui::Separator();

        // Dos columnas: Izquierda (Lista) | Derecha (Ficha + Proxy)
        ImGui::Columns(2, "ColumnasCatalogo", true);

        ImGui::BeginChild("PanelLista");
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1.0f), "%s",
                           tituloLista.c_str());
        ImGui::Spacing();

        std::vector<pf::Resultado> listaMostrar = resultados;
        if (listaMostrar.empty() && tituloLista == "RECOMENDADAS PARA TI") {
          listaMostrar = motor->recomendador().paraUsuario(
              estadoUsuario.meGusta(), estadoUsuario.verMasTarde(), 15);
        }

        if (listaMostrar.empty()) {
          ImGui::TextWrapped(
              "Realiza una busqueda arriba o marca 'Me gusta' en una "
              "pelicula para generar recomendaciones personalizadas.");
        } else {
          for (size_t i = 0; i < listaMostrar.size(); ++i) {
            const auto &r = listaMostrar[i];
            const auto &p = motor->catalogo()[r.doc];
            std::string item = std::to_string(i + 1) + ". " + p.titulo;
            if (p.anio > 0) item += " (" + std::to_string(p.anio) + ")";
            if (estadoUsuario.tieneMeGusta(r.doc)) item += " [<3]";
            if (estadoUsuario.tieneVerMasTarde(r.doc)) item += " [+]";
            item += "##doc" + std::to_string(r.doc);

            if (ImGui::Selectable(item.c_str(), docSeleccionado == r.doc)) {
              docSeleccionado = r.doc;
              mensajeProxy.clear();
            }
            ImGui::TextDisabled("   Score: %.2f | %s", r.puntaje,
                                r.explicacion.c_str());
          }
        }
        ImGui::EndChild();

        ImGui::NextColumn();

        ImGui::BeginChild("PanelDetalle");
        if (docSeleccionado >= 0 &&
            docSeleccionado < static_cast<int>(motor->catalogo().tamano())) {
          const auto &p = motor->catalogo()[docSeleccionado];
          ImGui::TextWrapped("%s (%d)", p.titulo.c_str(), p.anio);
          ImGui::Separator();

          if (!p.director.empty())
            ImGui::Text("Director : %s", p.director.c_str());
          if (!p.generos.empty())
            ImGui::Text("Generos  : %s", unirLista(p.generos).c_str());
          if (!p.reparto.empty())
            ImGui::TextWrapped("Reparto  : %s", unirLista(p.reparto, ", ", 6).c_str());
          if (!p.origen.empty())
            ImGui::Text("Origen   : %s", p.origen.c_str());

          ImGui::Spacing();

          // Botones de usuario con PATRON MEMENTO
          bool esLike = estadoUsuario.tieneMeGusta(docSeleccionado);
          if (ImGui::Button(esLike ? "Quitar Me gusta" : "Me gusta [<3]")) {
            historialMemento.agregarEstado(gestorEstado.guardarEstado());
            gestorEstado.alternarMeGusta(docSeleccionado);
            estadoUsuario.guardar(rutaEstado, motor->catalogo());
          }
          ImGui::SameLine();

          bool esVer = estadoUsuario.tieneVerMasTarde(docSeleccionado);
          if (ImGui::Button(esVer ? "Quitar Ver mas tarde"
                                  : "Ver mas tarde [+]")) {
            historialMemento.agregarEstado(gestorEstado.guardarEstado());
            gestorEstado.alternarVerMasTarde(docSeleccionado);
            estadoUsuario.guardar(rutaEstado, motor->catalogo());
          }
          ImGui::SameLine();

          if (ImGui::Button("Buscar Similares")) {
            resultados =
                motor->recomendador().similaresA(docSeleccionado, 15);
            tituloLista = "SIMILARES A: " + p.titulo;
          }

          ImGui::Spacing();
          ImGui::Separator();

          // Controles y ejecucion del PATRON PROXY
          ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f),
                             "CONTROL DE ACCESO (Patron Proxy)");
          ImGui::Checkbox("Suscripcion Activa", &cuentaActiva);
          ImGui::SameLine();
          ImGui::Checkbox("Region Permitida", &regionPermitida);

          if (ImGui::Button("Reproducir Pelicula (via Proxy)")) {
            pf::ProxyPelicula proxy(p, docSeleccionado, cuentaActiva,
                                    regionPermitida, estadoUsuario);
            mensajeProxy = proxy.reproducir();
            estadoUsuario.guardar(rutaEstado, motor->catalogo());
          }
          if (!mensajeProxy.empty()) {
            ImGui::TextWrapped("%s", mensajeProxy.c_str());
          }

          ImGui::Spacing();
          ImGui::Separator();
          ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1.0f), "SINOPSIS");
          ImGui::TextWrapped("%s", p.sinopsis.c_str());
        } else {
          ImGui::TextDisabled(
              "Selecciona una pelicula de la columna izquierda para ver su "
              "ficha completa.");
        }
        ImGui::EndChild();

        ImGui::Columns(1);
        ImGui::EndTabItem();
      }

      // ----------------------------------------------------------------------
      // TAB 2: MIS LISTAS (Me gusta / Ver mas tarde)
      // ----------------------------------------------------------------------
      if (ImGui::BeginTabItem("Mis Listas")) {
        ImGui::Columns(2, "ListasUsuario", true);

        ImGui::BeginChild("ColMeGusta");
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.5f, 1.0f), "ME GUSTA (%zu)",
                           estadoUsuario.meGusta().size());
        ImGui::Separator();
        for (int doc : estadoUsuario.meGusta()) {
          const auto &p = motor->catalogo()[doc];
          if (ImGui::Selectable((p.titulo + " (" + std::to_string(p.anio) +
                                 ")##mg" + std::to_string(doc))
                                    .c_str())) {
            docSeleccionado = doc;
          }
        }
        ImGui::EndChild();

        ImGui::NextColumn();

        ImGui::BeginChild("ColVerMasTarde");
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                           "VER MAS TARDE (%zu)",
                           estadoUsuario.verMasTarde().size());
        ImGui::Separator();
        for (int doc : estadoUsuario.verMasTarde()) {
          const auto &p = motor->catalogo()[doc];
          if (ImGui::Selectable((p.titulo + " (" + std::to_string(p.anio) +
                                 ")##vmt" + std::to_string(doc))
                                    .c_str())) {
            docSeleccionado = doc;
          }
        }
        ImGui::EndChild();

        ImGui::Columns(1);
        ImGui::EndTabItem();
      }

      // ----------------------------------------------------------------------
      // TAB 3: BENCHMARK DE PROGRAMACION PARALELA Y ESTRUCTURAS
      // ----------------------------------------------------------------------
      if (ImGui::BeginTabItem("Rendimiento Paralelo & Arboles")) {
        ImGui::TextWrapped(
            "Prueba comparativa de insercion/tokenizacion de strings en Trie y "
            "busqueda de frases ejecutando en 1 hilo (secuencial) vs todos "
            "los nucleos disponibles de tu procesador.");
        ImGui::Spacing();

        if (ImGui::Button("Ejecutar Benchmark Secuencial vs. Paralelo")) {
          ultimoBench = pf::MotorParalelo::ejecutarBenchmark(
              motor->catalogo(), motor->buscador());
          hayBenchmark = true;
        }

        if (hayBenchmark) {
          ImGui::Spacing();
          ImGui::Separator();
          ImGui::Text("Hilos de hardware utilizados : %u",
                      ultimoBench.numHilos);
          ImGui::Text("Strings unicos procesados    : %zu",
                      ultimoBench.stringsProcesados);
          ImGui::Spacing();

          ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f),
                             "1. INSERCION Y TOKENIZACION DE STRINGS:");
          ImGui::Text("   Secuencial (1 hilo) : %.2f ms",
                      ultimoBench.msInsercionSecuencial);
          ImGui::Text("   Paralelo (%u hilos)  : %.2f ms",
                      ultimoBench.numHilos, ultimoBench.msInsercionParalela);
          if (ultimoBench.msInsercionParalela > 0.0) {
            ImGui::Text("   Aceleracion (Speedup): %.2fx",
                        ultimoBench.msInsercionSecuencial /
                            ultimoBench.msInsercionParalela);
          }

          ImGui::Spacing();
          ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
                             "2. BUSQUEDA DE STRINGS (LOTE DE 16 CONSULTAS):");
          ImGui::Text("   Secuencial (1 hilo) : %.2f ms",
                      ultimoBench.msBusquedaSecuencial);
          ImGui::Text("   Paralelo (%u hilos)  : %.2f ms",
                      ultimoBench.numHilos, ultimoBench.msBusquedaParalela);
          if (ultimoBench.msBusquedaParalela > 0.0) {
            ImGui::Text("   Aceleracion (Speedup): %.2fx",
                        ultimoBench.msBusquedaSecuencial /
                            ultimoBench.msBusquedaParalela);
          }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.95f, 0.8f, 0.3f, 1.0f),
                           "ESTADISTICAS DE ARBOLES EN MEMORIA:");
        ImGui::Text("   Trie de Prefijos : %zu nodos | %zu terminos",
                    motor->indice().vocabulario().numNodos(),
                    motor->indice().vocabulario().numTerminos());
        ImGui::Text("   Trie de Sufijos  : %zu nodos",
                    motor->indice().sufijos().numNodos());
        ImGui::Text("   Arbol AVL Anios  : %zu nodos | Altura balanceada: %d",
                    motor->arbolAnios().totalNodos(),
                    motor->arbolAnios().alturaTotal());
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::End();

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}