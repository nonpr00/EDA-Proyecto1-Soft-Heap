"""
Visualizacion de resultados del Soft Heap sobre Miraflores.

Capas del mapa:
  1. Mapa de calor Dijkstra (intensidad = distancia desde origen)
  2. Aristas del grafo (todas, en gris)
  3. Camino mas corto Dijkstra exacto (azul)
  4. Camino mas corto Dijkstra SoftHeap (morado)
  5. Camino mas rapido Dijkstra exacto (naranja)
  6. MST Prim exacto (verde)
  7. MST Prim SoftHeap (lima)
"""
import json, sys, folium
from folium.plugins import HeatMap

fname = sys.argv[1] if len(sys.argv) > 1 else "resultados_miraflores.json"
with open(fname) as f:
    data = json.load(f)

vertices = data["vertices"]
src      = data["meta"]["src"]
n        = data["meta"]["n_nodes"]

print(f"Nodos: {n}  Origen: {src}")

center_lat = sum(v["lat"] for v in vertices) / len(vertices)
center_lon = sum(v["lon"] for v in vertices) / len(vertices)

mapa = folium.Map(
    location=[center_lat, center_lon],
    zoom_start=15,
    tiles="CartoDB dark_matter"
)

# ============================================================
# Reconstruye camino src→dst desde prev[]
# ============================================================
def get_path(prev, dst):
    path = []
    v = dst
    visited = set()
    while v != -1 and v not in visited:
        path.append(v)
        visited.add(v)
        v = prev[v] if v < len(prev) else -1
    path.reverse()
    return path

# ============================================================
# Elegimos destino = nodo mas lejano alcanzable
# ============================================================
dd_std = next(d for d in data["dijkstra_dist"] if d["label"] == "std")
dd_sh  = next(d for d in data["dijkstra_dist"] if d["label"] == "sh0.1")
dt_std = next(d for d in data["dijkstra_time"] if d["label"] == "std")
dt_sh  = next(d for d in data["dijkstra_time"] if d["label"] == "sh0.1")

dist_all = dd_std["dist_all"]
dst = max(
    range(n),
    key=lambda i: dist_all[i] if dist_all[i] is not None else -1
)
print(f"Destino mas lejano: nodo {dst}  dist={dist_all[dst]:.0f}m")

# ============================================================
# CAPA 0 — Todas las aristas del grafo (fondo gris)
# ============================================================
g_all = folium.FeatureGroup(name="Red de calles (gris)", show=True)

# Reconstruimos las aristas desde prev[] de todos los nodos
# (usamos adj implicita: si prev[v]=u entonces existe arista u-v)
# Para no duplicar, marcamos pares ya dibujados
drawn = set()
for v in range(n):
    u = dd_std["prev"][v]
    if u == -1 or u >= len(vertices) or v >= len(vertices):
        continue
    key = (min(u, v), max(u, v))
    if key in drawn:
        continue
    drawn.add(key)
    folium.PolyLine(
        locations=[
            [vertices[u]["lat"], vertices[u]["lon"]],
            [vertices[v]["lat"], vertices[v]["lon"]]
        ],
        color="#2c3e50",
        weight=1.5,
        opacity=0.6
    ).add_to(g_all)
g_all.add_to(mapa)

# ============================================================
# CAPA 1 — Mapa de calor Dijkstra (distancia desde origen)
# ============================================================
g_heat = folium.FeatureGroup(name="Heatmap distancias desde origen",
                              show=True)
valid = [d for d in dist_all if d is not None and d > 0]
max_d = max(valid) if valid else 1

heat_points = []
for i, v in enumerate(vertices):
    d = dist_all[i] if i < len(dist_all) else None
    if d is None:
        continue
    intensity = 1.0 - (d / max_d)
    heat_points.append([v["lat"], v["lon"], intensity])

HeatMap(
    heat_points,
    min_opacity=0.3,
    max_zoom=18,
    radius=18,
    blur=12,
    gradient={0.0: "blue", 0.4: "cyan",
              0.65: "lime", 0.85: "yellow", 1.0: "red"}
).add_to(g_heat)
g_heat.add_to(mapa)

# ============================================================
# CAPA 2 — Dijkstra exacto por distancia (azul)
#          Muestra el camino mas corto src→dst
# ============================================================
g_dijk_dist = folium.FeatureGroup(
    name=f"📍 Dijkstra exacto distancia → nodo {dst}", show=True)

path_dd = get_path(dd_std["prev"], dst)
coords_dd = [[vertices[v]["lat"], vertices[v]["lon"]]
              for v in path_dd if v < len(vertices)]

if len(coords_dd) >= 2:
    folium.PolyLine(
        locations=coords_dd,
        color="#3498db",
        weight=7,
        opacity=0.95,
        tooltip=f"<b>Dijkstra exacto (distancia)</b><br>"
                f"Dist: {dist_all[dst]:.0f}m<br>"
                f"Nodos en ruta: {len(path_dd)}<br>"
                f"Tiempo algoritmo: {dd_std['ms']:.3f}ms"
    ).add_to(g_dijk_dist)
    # Flechas en el camino para mostrar dirección
    for i in range(len(coords_dd)-1):
        mid_lat = (coords_dd[i][0] + coords_dd[i+1][0]) / 2
        mid_lon = (coords_dd[i][1] + coords_dd[i+1][1]) / 2
        folium.CircleMarker(
            location=[mid_lat, mid_lon],
            radius=3,
            color="#3498db",
            fill=True,
            fill_opacity=0.8
        ).add_to(g_dijk_dist)
g_dijk_dist.add_to(mapa)

# ============================================================
# CAPA 3 — Dijkstra SoftHeap por distancia (morado)
# ============================================================
g_dijk_sh = folium.FeatureGroup(
    name=f"📍 Dijkstra SoftHeap ε = 0.1 distancia → nodo {dst}",
    show=True)

path_sh = get_path(dd_sh["prev"], dst)
coords_sh = [[vertices[v]["lat"], vertices[v]["lon"]]
              for v in path_sh if v < len(vertices)]

if len(coords_sh) >= 2:
    dist_sh = dd_sh["dist_all"][dst]
    err_dijk = abs((dist_sh or 0) - (dist_all[dst] or 0))
    folium.PolyLine(
        locations=coords_sh,
        color="#9b59b6",
        weight=5,
        opacity=0.85,
        dash_array="8 4",
        tooltip=f"<b>Dijkstra SoftHeap ε=0.1</b><br>"
                f"Dist: {dist_sh:.0f}m<br>"
                f"Error vs exacto: {err_dijk:.1f}m<br>"
                f"Tiempo algoritmo: {dd_sh['ms']:.3f}ms"
    ).add_to(g_dijk_sh)
g_dijk_sh.add_to(mapa)

# ============================================================
# CAPA 4 — Dijkstra exacto por TIEMPO (naranja)
# ============================================================
g_dijk_time = folium.FeatureGroup(
    name=f"⏱ Dijkstra exacto TIEMPO → nodo {dst}", show=False)

path_dt = get_path(dt_std["prev"], dst)
coords_dt = [[vertices[v]["lat"], vertices[v]["lon"]]
              for v in path_dt if v < len(vertices)]

if len(coords_dt) >= 2:
    t_dst = dt_std["dist_all"][dst]
    folium.PolyLine(
        locations=coords_dt,
        color="#e67e22",
        weight=7,
        opacity=0.95,
        tooltip=f"<b>Dijkstra exacto (tiempo)</b><br>"
                f"Tiempo: {t_dst:.0f}s ({t_dst/60:.1f}min)<br>"
                f"Nodos en ruta: {len(path_dt)}<br>"
                f"Nota: puede diferir de la ruta por distancia"
    ).add_to(g_dijk_time)
g_dijk_time.add_to(mapa)

# ============================================================
# CAPA 5 — MST Prim exacto (verde)
#          Conecta TODOS los nodos del grafo
# ============================================================
pm_std = next(p for p in data["prim_dist"] if p["label"] == "std")
pm_sh  = next(p for p in data["prim_dist"] if p["label"] == "sh0.1")

g_mst = folium.FeatureGroup(
    name=f"MST Prim exacto ({pm_std['total_w']:.0f}m, "
         f"{len(pm_std['mst_edges'])} aristas)",
    show=False)

for u, v in pm_std["mst_edges"]:
    if u >= len(vertices) or v >= len(vertices):
        continue
    folium.PolyLine(
        locations=[
            [vertices[u]["lat"], vertices[u]["lon"]],
            [vertices[v]["lat"], vertices[v]["lon"]]
        ],
        color="#2ecc71",
        weight=3,
        opacity=0.85,
        tooltip=f"MST Prim: {u}↔{v}"
    ).add_to(g_mst)
g_mst.add_to(mapa)

# ============================================================
# CAPA 6 — MST Prim SoftHeap (lima)
# ============================================================
err_mst = abs(pm_sh["total_w"] - pm_std["total_w"])
g_mst_sh = folium.FeatureGroup(
    name=f"MST Prim SH ε = 0.1 ({pm_sh['total_w']:.0f}m, "
         f"Δ{err_mst:.1f}m)",
    show=False)

for u, v in pm_sh["mst_edges"]:
    if u >= len(vertices) or v >= len(vertices):
        continue
    folium.PolyLine(
        locations=[
            [vertices[u]["lat"], vertices[u]["lon"]],
            [vertices[v]["lat"], vertices[v]["lon"]]
        ],
        color="#a8e063",
        weight=3,
        opacity=0.85,
        dash_array="6 3",
        tooltip=f"MST Prim SH: {u}↔{v}"
    ).add_to(g_mst_sh)
g_mst_sh.add_to(mapa)

# ============================================================
# MARCADORES especiales
# ============================================================
# Origen (verde grande)
folium.CircleMarker(
    location=[vertices[src]["lat"], vertices[src]["lon"]],
    radius=14, color="white", fill=True,
    fill_color="#27ae60", fill_opacity=1,
    tooltip=f"<b>ORIGEN Dijkstra</b><br>Nodo {src}<br>"
            f"Distancias calculadas a los {n-1} nodos restantes"
).add_to(mapa)

folium.Marker(
    location=[vertices[src]["lat"], vertices[src]["lon"]],
    icon=folium.DivIcon(
        html=f'<div style="color:white;font-weight:bold;'
             f'font-size:11px;text-shadow:1px 1px 2px black;">SRC</div>',
        icon_size=(30,15), icon_anchor=(15,7)
    )
).add_to(mapa)

# Destino (rojo grande)
folium.CircleMarker(
    location=[vertices[dst]["lat"], vertices[dst]["lon"]],
    radius=12, color="white", fill=True,
    fill_color="#e74c3c", fill_opacity=1,
    tooltip=f"<b>Destino más lejano</b><br>Nodo {dst}<br>"
            f"Distancia exacta: {dist_all[dst]:.0f}m"
).add_to(mapa)

folium.Marker(
    location=[vertices[dst]["lat"], vertices[dst]["lon"]],
    icon=folium.DivIcon(
        html=f'<div style="color:white;font-weight:bold;'
             f'font-size:11px;text-shadow:1px 1px 2px black;">DST</div>',
        icon_size=(30,15), icon_anchor=(15,7)
    )
).add_to(mapa)

# Todos los nodos del grafo (pequeños)
g_nodes = folium.FeatureGroup(name="Nodos del grafo", show=False)
for i, v in enumerate(vertices):
    if i == src or i == dst:
        continue
    d = dist_all[i] if i < len(dist_all) and dist_all[i] is not None else 0
    folium.CircleMarker(
        location=[v["lat"], v["lon"]],
        radius=4, color="#ecf0f1", fill=True,
        fill_color="#bdc3c7", fill_opacity=0.7,
        tooltip=f"Nodo {i}  dist={d:.0f}m"
    ).add_to(g_nodes)
g_nodes.add_to(mapa)

# ============================================================
# LEYENDA
# ============================================================
paths_same = path_dd == path_dt
leyenda = f"""
<div style="position:fixed;bottom:25px;left:25px;z-index:9999;
  background:#1a1a2e;padding:16px 20px;border-radius:12px;
  color:white;font-family:Helvetica,Arial;font-size:12px;
  border:1px solid #404060;min-width:310px;
  box-shadow:0 4px 20px rgba(0,0,0,0.6);">
  <div style="font-size:14px;font-weight:bold;margin-bottom:12px;">
    Soft Heap - Miraflores, Lima
  </div>

  <div style="font-size:11px;color:#95a5a6;margin-bottom:4px;">
    DIJKSTRA: 1 origen → {n-1} destinos
  </div>
  <div style="margin-bottom:5px;">
    Heatmap: <b>rojo=cerca, azul=lejos</b>
  </div>
  <div style="margin-bottom:5px;">
    <span style="color:#3498db">━━━</span>
    Ruta exacta dist: <b>{dist_all[dst]:.0f}m</b>
    <span style="color:#95a5a6;font-size:10px">
      {dd_std['ms']:.3f}ms
    </span>
  </div>
  <div style="margin-bottom:5px;">
    <span style="color:#9b59b6">╌╌╌</span>
    Ruta SH ε = 0.1: <b>{dd_sh['dist_all'][dst]:.0f}m</b>
    <span style="color:#95a5a6;font-size:10px">
      {dd_sh['ms']:.3f}ms
    </span>
  </div>
  <div style="margin-bottom:10px;">
    <span style="color:#e67e22">━━━</span>
    Ruta exacta tiempo: <b>{dt_std['dist_all'][dst]:.0f}s</b>
    {'<span style="color:#e74c3c;font-size:10px"> (ruta diferente)</span>'
     if not paths_same else
     '<span style="color:#2ecc71;font-size:10px"> (mismo camino)</span>'}
  </div>

  <div style="font-size:11px;color:#95a5a6;margin-bottom:4px;">
    PRIM MST: conecta los {n} nodos
  </div>
  <div style="margin-bottom:5px;">
    <span style="color:#2ecc71">━━━</span>
    Exacto: <b>{pm_std['total_w']:.0f}m</b>
    ({len(pm_std['mst_edges'])} aristas)
    <span style="color:#95a5a6;font-size:10px">
      {pm_std['ms']:.3f}ms
    </span>
  </div>
  <div style="margin-bottom:10px;">
    <span style="color:#a8e063">╌╌╌</span>
    SH ε = 0.1: <b>{pm_sh['total_w']:.0f}m</b>
    <span style="color:{'#e74c3c' if err_mst>0 else '#2ecc71'};font-size:10px">
      (Δ{err_mst:.1f}m)
    </span>
  </div>

  <div style="font-size:10px;color:#7f8c8d;border-top:1px solid #404060;
              padding-top:8px;margin-top:4px;">
    Indicaciones:<br>
    <span style="color:#27ae60">⬤</span> Origen &nbsp;
    <span style="color:#e74c3c">⬤</span> Destino más lejano
  </div>
</div>
"""
mapa.get_root().html.add_child(folium.Element(leyenda))
folium.LayerControl(collapsed=False).add_to(mapa)

out = "miraflores_softHeap.html"
mapa.save(out)
print(f"\nGuardado: {out}")
print(f"\nResumen visual:")
print(f"  Dijkstra exacto dist:  {dist_all[dst]:.0f}m  ({dd_std['ms']:.3f}ms)")
print(f"  Dijkstra SH ε=0.1:     {dd_sh['dist_all'][dst]:.0f}m  ({dd_sh['ms']:.3f}ms)")
print(f"  Dijkstra exacto tiempo:{dt_std['dist_all'][dst]:.0f}s  ({dt_std['ms']:.3f}ms)")
print(f"  MST Prim exacto:       {pm_std['total_w']:.0f}m  {len(pm_std['mst_edges'])} aristas")
print(f"  MST Prim SH ε=0.1:     {pm_sh['total_w']:.0f}m  Δ{err_mst:.1f}m")