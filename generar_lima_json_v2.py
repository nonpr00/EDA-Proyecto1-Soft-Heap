"""
Script local
Exporta el grafo de Miraflores con:
  - length      : distancia en metros
  - travel_time : tiempo en segundos (con velocidades imputadas)
  - speed_kph   : velocidad en km/h
  - highway     : tipo de via
  - name        : nombre de la calle
"""
import osmnx as ox
import json

def exportar_con_tiempos(lugar, archivo_salida):
    print(f"Descargando grafo de '{lugar}'...")
    G = ox.graph_from_place(lugar, network_type="drive")

    print("Calculando velocidades y tiempos de viaje...")
    # Imputa velocidades por tipo de via (km/h)
    G = ox.add_edge_speeds(G)
    # Calcula travel_time = length / speed  (en segundos)
    G = ox.add_edge_travel_times(G)

    print(f"Nodos (intersecciones): {len(G.nodes)}")
    print(f"Aristas (segmentos):    {len(G.edges)}")

    # Mapeamos IDs de OSM a indices 0..n-1
    nodos_osm = list(G.nodes)
    osm_a_idx = {osm_id: i for i, osm_id in enumerate(nodos_osm)}

    nodos_json = []
    for osm_id, data in G.nodes(data=True):
        nodos_json.append({
            "id":  osm_a_idx[osm_id],
            "osm": osm_id,
            "lat": round(data.get("y", 0.0), 6),
            "lon": round(data.get("x", 0.0), 6),
        })

    aristas_json = []
    for u, v, data in G.edges(data=True):
        aristas_json.append({
            "from":        osm_a_idx[u],
            "to":          osm_a_idx[v],
            # PESO 1: distancia en metros (para Dijkstra por distancia)
            "length_m":    round(data.get("length", 0.0), 2),
            # PESO 2: tiempo en segundos (para Dijkstra por tiempo)
            "time_s":      round(data.get("travel_time", 0.0), 2),
            # Info extra
            "speed_kph":   round(data.get("speed_kph", 0.0), 1),
            "highway":     str(data.get("highway", "")),
            "name":        str(data.get("name", "")),
            "oneway":      data.get("oneway", False),
        })

    grafo = {
        "lugar":       lugar,
        "n_nodos":     len(nodos_json),
        "n_aristas":   len(aristas_json),
        "descripcion": {
            "length_m": "Distancia del segmento en metros",
            "time_s":   "Tiempo de viaje en segundos (flujo libre)",
            "speed_kph":"Velocidad imputada en km/h",
            "highway":  "Tipo de via segun OpenStreetMap",
        },
        "vertices": nodos_json,
        "edges":    aristas_json,
    }

    with open(archivo_salida, "w", encoding="utf-8") as f:
        json.dump(grafo, f, indent=2, ensure_ascii=False)

    print(f"\nGuardado en: {archivo_salida}")

    # Mostramos un ejemplo de arista
    ej = aristas_json[0]
    print(f"\nEjemplo de arista:")
    print(f"  Calle:    {ej['name']} ({ej['highway']})")
    print(f"  Distancia:{ej['length_m']} m")
    print(f"  Tiempo:   {ej['time_s']} s  ({ej['time_s']/60:.1f} min)")
    print(f"  Velocidad:{ej['speed_kph']} km/h")


if __name__ == "__main__":
    # Miraflores: ~2000 nodos, rapido de descargar
    exportar_con_tiempos(
        lugar          = "Miraflores, Lima, Peru",
        archivo_salida = "graph_miraflores.json"
    )

    # Para Lima completa (~80k nodos, tarda ~2 min):
    # exportar_con_tiempos("Lima, Peru", "graph_lima.json")```