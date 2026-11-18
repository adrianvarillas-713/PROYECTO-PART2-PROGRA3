#pragma once

#include <memory>
#include <string>
#include <vector>

#include "buscador.hpp"
#include "catalogo.hpp"
#include "estado_usuario.hpp"
#include "indice.hpp"
#include "recomendador.hpp"

namespace pf {

// ============================================================================
// 1. PATRON DE CREACION: SINGLETON (Teoria 10)
// Garantiza una unica instancia en memoria del catalogo, indice y buscador.
// ============================================================================
class MotorStream {
private:
  static MotorStream *instancia_;

  Catalogo catalogo_;
  Indice indice_;
  std::unique_ptr<Buscador> buscador_;
  std::unique_ptr<Recomendador> recomendador_;
  bool inicializado_ = false;

  // Constructor privado
  MotorStream() = default;

public:
  // Evitar copias del Singleton
  MotorStream(const MotorStream &) = delete;
  MotorStream &operator=(const MotorStream &) = delete;

  static MotorStream *getInstance();

  void inicializar(const std::string &rutaCsv, bool usarCache = true);

  const Catalogo &catalogo() const { return catalogo_; }
  const Indice &indice() const { return indice_; }
  const Buscador &buscador() const { return *buscador_; }
  const Recomendador &recomendador() const { return *recomendador_; }
};

// ============================================================================
// 2. PATRON DE CREACION: BUILDER (Teoria 10)
// Construye paso a paso una Consulta filtrada mediante encadenamiento.
// ============================================================================
class ConsultaBuilder {
private:
  std::string textoLibre_;
  std::string director_;
  std::string reparto_;
  std::string genero_;
  std::string origen_;
  int anio_ = -1;

public:
  ConsultaBuilder &texto(const std::string &t);
  ConsultaBuilder &director(const std::string &d);
  ConsultaBuilder &reparto(const std::string &r);
  ConsultaBuilder &genero(const std::string &g);
  ConsultaBuilder &origen(const std::string &o);
  ConsultaBuilder &anio(int a);

  // Construye y parsea la Consulta usando el Buscador activo
  Consulta build(const Buscador &buscador) const;
};

// ============================================================================
// 3. PATRON DE ESTRUCTURA: PROXY (Teoria 11)
// Controla el acceso (permisos/region) y audita antes de reproducir el video.
// ============================================================================
class IPeliculaReproducible {
public:
  virtual ~IPeliculaReproducible() = default;
  virtual std::string reproducir() = 0;
};

class PeliculaReal : public IPeliculaReproducible {
private:
  std::string titulo_;
  std::string url_;

public:
  PeliculaReal(std::string titulo, std::string url);
  std::string reproducir() override;
};

class ProxyPelicula : public IPeliculaReproducible {
private:
  PeliculaReal peliculaReal_;
  int docId_;
  bool cuentaActiva_;
  bool regionPermitida_;
  EstadoUsuario &estadoUsuario_;

public:
  ProxyPelicula(const Pelicula &pelicula, int docId, bool cuentaActiva,
                bool regionPermitida, EstadoUsuario &estadoUsuario);

  std::string reproducir() override;
};

// ============================================================================
// 4. PATRON DE COMPORTAMIENTO: MEMENTO (Teoria 11)
// Guarda y restaura puntos de restauracion de EstadoUsuario (Deshacer).
// ============================================================================
class MementoUsuario {
private:
  std::vector<int> meGusta_;
  std::vector<int> verMasTarde_;

public:
  MementoUsuario(std::vector<int> meGusta, std::vector<int> verMasTarde);

  const std::vector<int> &getMeGusta() const { return meGusta_; }
  const std::vector<int> &getVerMasTarde() const { return verMasTarde_; }
};

// Originator: crea y restaura instantaneas del EstadoUsuario
class GestorEstadoUsuario {
private:
  EstadoUsuario &estado_;

public:
  explicit GestorEstadoUsuario(EstadoUsuario &estado);

  void alternarMeGusta(int doc);
  void alternarVerMasTarde(int doc);

  MementoUsuario guardarEstado() const;
  void restaurarEstado(const MementoUsuario &memento);

  const EstadoUsuario &estado() const { return estado_; }
};

// Caretaker (Historial): administra la pila de estados guardados
class HistorialUsuario {
private:
  std::vector<MementoUsuario> estados_;

public:
  void agregarEstado(const MementoUsuario &estado);
  bool hayHistorial() const { return !estados_.empty(); }
  MementoUsuario getEstadoAnterior();
};

} // namespace pf