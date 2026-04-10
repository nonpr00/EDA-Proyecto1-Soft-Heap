#include "SoftHeap.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <queue>
#include <limits>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <string>
#include <stdexcept>
#include <cmath>

const double INF = std::numeric_limits<double>::infinity();

// ============================================================
// PARSER JSON
// ============================================================
static std::string trim_str(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n\"");
    size_t b = s.find_last_not_of(" \t\r\n\"");
    return (a == std::string::npos) ? "" : s.substr(a, b-a+1);
}
static std::string json_get(const std::string& block,
                              const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = block.find(needle);
    if (pos == std::string::npos) return "";
    pos = block.find(':', pos + needle.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < block.size() && (block[pos]==' '||block[pos]=='\t')) pos++;
    if (pos >= block.size()) return "";
    if (block[pos] == '"') {
        pos++;
        std::string val;
        while (pos < block.size() && block[pos] != '"') {
            if (block[pos]=='\\') pos++;
            if (pos < block.size()) val += block[pos];
            pos++;
        }
        return val;
    }
    size_t end = pos;
    while (end < block.size() && block[end]!=',' &&
           block[end]!='}' && block[end]!='\n' &&
           block[end]!='\r') end++;
    return trim_str(block.substr(pos, end-pos));
}
static std::vector<std::string> json_array_objects(
    const std::string& content, const std::string& array_key)
{
    std::vector<std::string> result;
    size_t start = content.find("\"" + array_key + "\"");
    if (start == std::string::npos) return result;
    start = content.find('[', start);
    if (start == std::string::npos) return result;
    start++;
    size_t pos = start; int depth = 0;
    size_t obj_start = std::string::npos;
    while (pos < content.size()) {
        char c = content[pos];
        if (c=='{') { if (depth==0) obj_start=pos; depth++; }
        else if (c=='}') {
            depth--;
            if (depth==0 && obj_start!=std::string::npos) {
                result.push_back(content.substr(obj_start,pos-obj_start+1));
                obj_start=std::string::npos;
            }
        } else if (c==']' && depth==0) break;
        pos++;
    }
    return result;
}

// ============================================================
// ESTRUCTURAS
// ============================================================
struct Vertex { int id; double lat, lon; };
struct GEdge  {
    int from, to;
    double length_m, time_s, speed_kph;
    std::string name, highway;
};
struct GeoGraph {
    int n;
    std::vector<Vertex> vertices;
    std::vector<GEdge>  all_edges;
    std::vector<std::vector<std::pair<int,double>>> adj_dist;
    std::vector<std::vector<std::pair<int,double>>> adj_time;
};

GeoGraph load_osmnx_json(const std::string& filename) {
    std::ifstream f(filename);
    if (!f.is_open()) throw std::runtime_error("No se pudo abrir: "+filename);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    GeoGraph g;
    std::string sn = json_get(content,"n_nodos");
    if (sn.empty()) sn = json_get(content,"nodos");
    g.n = sn.empty() ? 0 : std::stoi(sn);

    for (auto& obj : json_array_objects(content,"vertices")) {
        std::string sid = json_get(obj,"id");
        if (sid.empty()) continue;
        Vertex v; v.id = std::stoi(sid);
        std::string slat=json_get(obj,"lat"), slon=json_get(obj,"lon");
        v.lat = slat.empty()?0.0:std::stod(slat);
        v.lon = slon.empty()?0.0:std::stod(slon);
        g.vertices.push_back(v);
    }
    for (auto& obj : json_array_objects(content,"edges")) {
        std::string sf=json_get(obj,"from"), st=json_get(obj,"to");
        if (sf.empty()||st.empty()) continue;
        GEdge e; e.from=std::stoi(sf); e.to=std::stoi(st);
        std::string sl=json_get(obj,"length_m"),
                    ss=json_get(obj,"time_s"),
                    sw=json_get(obj,"weight");
        e.length_m = sl.empty()?(sw.empty()?1.0:std::stod(sw)):std::stod(sl);
        e.time_s   = ss.empty()?e.length_m/(30000.0/3600.0):std::stod(ss);
        std::string ssp=json_get(obj,"speed_kph");
        e.speed_kph= ssp.empty()?30.0:std::stod(ssp);
        e.name     = json_get(obj,"name");
        e.highway  = json_get(obj,"highway");
        if (e.from<0||e.to<0||e.from==e.to) continue;
        g.all_edges.push_back(e);
    }
    if (g.n==0) g.n=(int)g.vertices.size();
    g.adj_dist.resize(g.n);
    g.adj_time.resize(g.n);
    for (auto& e : g.all_edges) {
        if (e.from>=g.n||e.to>=g.n) continue;
        g.adj_dist[e.from].push_back({e.to, e.length_m});
        g.adj_time[e.from].push_back({e.to, e.time_s});
    }
    std::cout << "Grafo: " << g.vertices.size()
              << " vertices, " << g.all_edges.size() << " aristas\n";
    return g;
}

// ============================================================
// DIJKSTRA — 1 fuente → TODOS los nodos
// Retorna dist[] y prev[] para todos los nodos
// ============================================================
struct DijkResult {
    std::vector<double> dist;  // distancia minima a cada nodo
    std::vector<int>    prev;  // nodo anterior en el camino optimo
    double              ms;
};

DijkResult dijkstra_std(
    const std::vector<std::vector<std::pair<int,double>>>& adj,
    int n, int src)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    DijkResult r;
    r.dist.assign(n, INF);
    r.prev.assign(n, -1);
    std::priority_queue<
        std::pair<double,int>,
        std::vector<std::pair<double,int>>,
        std::greater<>> pq;
    r.dist[src] = 0;
    pq.push({0.0, src});
    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > r.dist[u]) continue;
        for (auto& [v, w] : adj[u])
            if (r.dist[u]+w < r.dist[v]) {
                r.dist[v] = r.dist[u]+w;
                r.prev[v] = u;
                pq.push({r.dist[v], v});
            }
    }
    r.ms = std::chrono::duration<double,std::milli>(
        std::chrono::high_resolution_clock::now()-t0).count();
    return r;
}

DijkResult dijkstra_sh(
    const std::vector<std::vector<std::pair<int,double>>>& adj,
    int n, int src, double eps)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    DijkResult r;
    r.dist.assign(n, INF);
    r.prev.assign(n, -1);
    SoftHeap<int> pq(eps);
    r.dist[src] = 0;
    pq.insert(0.0, src);
    while (!pq.is_empty()) {
        auto [d, u] = pq.delete_min();
        if (d > r.dist[u]+1e-9) continue;
        for (auto& [v, w] : adj[u])
            if (r.dist[u]+w < r.dist[v]) {
                r.dist[v] = r.dist[u]+w;
                r.prev[v] = u;
                pq.insert(r.dist[v], v);
            }
    }
    r.ms = std::chrono::duration<double,std::milli>(
        std::chrono::high_resolution_clock::now()-t0).count();
    return r;
}

// ============================================================
// PRIM — MST que conecta TODOS los nodos
// Retorna parent[] donde parent[v]=u significa arista u-v en MST
// ============================================================
struct PrimResult {
    double           total_w;  // peso total del MST
    std::vector<int> parent;   // arbol: parent[v] = predecesor de v
    double           ms;
};

PrimResult prim_std(
    const std::vector<std::vector<std::pair<int,double>>>& adj,
    int n, int src=0)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    PrimResult r; r.total_w=0;
    r.parent.assign(n,-1);
    std::vector<double> key(n,INF);
    std::vector<bool>   inMST(n,false);
    std::priority_queue<
        std::pair<double,int>,
        std::vector<std::pair<double,int>>,
        std::greater<>> pq;
    key[src]=0; pq.push({0.0,src});
    while (!pq.empty()) {
        auto [d,u] = pq.top(); pq.pop();
        if (inMST[u]) continue;
        inMST[u]=true; r.total_w+=d;
        for (auto& [v,w] : adj[u])
            if (!inMST[v] && w<key[v]) {
                key[v]=w; r.parent[v]=u; pq.push({w,v});
            }
    }
    r.ms = std::chrono::duration<double,std::milli>(
        std::chrono::high_resolution_clock::now()-t0).count();
    return r;
}

PrimResult prim_sh(
    const std::vector<std::vector<std::pair<int,double>>>& adj,
    int n, double eps, int src=0)
{
    auto t0 = std::chrono::high_resolution_clock::now();
    PrimResult r; r.total_w=0;
    r.parent.assign(n,-1);
    std::vector<double> key(n,INF);
    std::vector<bool>   inMST(n,false);
    SoftHeap<int> pq(eps);
    key[src]=0; pq.insert(0.0,src);
    while (!pq.is_empty()) {
        auto [d,u] = pq.delete_min(); // cuesta O(log V), ocurre V veces
        if (inMST[u]) continue;
        inMST[u]=true; r.total_w+=key[u];
        for (auto& [v,w] : adj[u])
            if (!inMST[v] && w<key[v]) {
                key[v]=w; r.parent[v]=u; 
                pq.insert(w,v); // cuesta O(log V), ocurre ≤ E veces
            }
    }
    r.ms = std::chrono::duration<double,std::milli>(
        std::chrono::high_resolution_clock::now()-t0).count();
    return r;
}

// Reconstruye camino src→dst desde prev[]
std::vector<int> get_path(const std::vector<int>& prev, int dst) {
    std::vector<int> path;
    for (int v=dst; v!=-1; v=prev[v]) path.push_back(v);
    std::reverse(path.begin(), path.end());
    return path;
}

// parent[] → lista de aristas del MST
std::vector<std::pair<int,int>> parent_to_edges(
    const std::vector<int>& parent)
{
    std::vector<std::pair<int,int>> edges;
    for (int v=0; v<(int)parent.size(); v++)
        if (parent[v]!=-1) edges.push_back({parent[v],v});
    return edges;
}

// ============================================================
// GUARDAR JSON de resultados
// ============================================================
void save_results_json(
    const GeoGraph& g,
    const DijkResult& rd_dist, const DijkResult& rsh_dist,
    const DijkResult& rd_time, const DijkResult& rsh_time,
    const PrimResult& pm_dist, const PrimResult& pmsh_dist,
    const PrimResult& pm_time, const PrimResult& pmsh_time,
    int src,
    const std::string& filename)
{
    std::ofstream f(filename);
    f << std::fixed << std::setprecision(6);
    f << "{\n";

    // Metadatos
    f << "  \"meta\": {\n";
    f << "    \"n_nodes\": " << g.n << ",\n";
    f << "    \"n_edges\": " << g.all_edges.size() << ",\n";
    f << "    \"src\": " << src << "\n";
    f << "  },\n";

    // Vertices con coordenadas GPS
    f << "  \"vertices\": [\n";
    for (int i=0; i<(int)g.vertices.size(); i++) {
        f << "    {\"id\":" << g.vertices[i].id
          << ",\"lat\":" << g.vertices[i].lat
          << ",\"lon\":" << g.vertices[i].lon << "}";
        if (i+1<(int)g.vertices.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n";

    // Helper: escribe dist[] y prev[] de Dijkstra
    // dist[] contiene la distancia a TODOS los nodos desde src
    auto write_dijk = [&](const std::string& lbl,
                          const DijkResult& r) {
        f << "    {\n";
        f << "      \"label\": \"" << lbl << "\",\n";
        f << "      \"ms\": " << r.ms << ",\n";
        f << "      \"src\": " << src << ",\n";

        // Distancias a todos los nodos (null si inalcanzable)
        f << "      \"dist_all\": [";
        for (int i=0; i<(int)r.dist.size(); i++) {
            if (r.dist[i]>1e15) f << "null";
            else f << std::setprecision(2) << r.dist[i];
            if (i+1<(int)r.dist.size()) f << ",";
        }
        f << "],\n";

        // prev[] para reconstruir cualquier camino desde src
        f << "      \"prev\": [";
        for (int i=0; i<(int)r.prev.size(); i++) {
            f << r.prev[i];
            if (i+1<(int)r.prev.size()) f << ",";
        }
        f << "]\n";
        f << "    }";
    };

    // Helper: escribe MST de Prim
    auto write_prim = [&](const std::string& lbl,
                          const PrimResult& r) {
        auto mst = parent_to_edges(r.parent);
        f << "    {\n";
        f << "      \"label\": \"" << lbl << "\",\n";
        f << "      \"ms\": " << r.ms << ",\n";
        f << "      \"total_w\": " << r.total_w << ",\n";
        // Aristas del MST: conectan TODOS los nodos del grafo
        f << "      \"mst_edges\": [";
        for (int i=0; i<(int)mst.size(); i++) {
            f << "[" << mst[i].first << "," << mst[i].second << "]";
            if (i+1<(int)mst.size()) f << ",";
        }
        f << "]\n";
        f << "    }";
    };

    // Dijkstra por distancia (metros)
    f << "  \"dijkstra_dist\": [\n";
    write_dijk("std",   rd_dist);  f << ",\n";
    write_dijk("sh0.1", rsh_dist); f << "\n";
    f << "  ],\n";

    // Dijkstra por tiempo (segundos)
    f << "  \"dijkstra_time\": [\n";
    write_dijk("std",   rd_time);  f << ",\n";
    write_dijk("sh0.1", rsh_time); f << "\n";
    f << "  ],\n";

    // Prim por distancia
    f << "  \"prim_dist\": [\n";
    write_prim("std",   pm_dist);   f << ",\n";
    write_prim("sh0.1", pmsh_dist); f << "\n";
    f << "  ],\n";

    // Prim por tiempo
    f << "  \"prim_time\": [\n";
    write_prim("std",   pm_time);   f << ",\n";
    write_prim("sh0.1", pmsh_time); f << "\n";
    f << "  ]\n";

    f << "}\n";
    f.close();
    std::cout << "Guardado: " << filename << "\n";
}

// ============================================================
// MAIN
// ============================================================
int main(int argc, char* argv[]) {
    std::string json_file = argc>1 ? argv[1] : "graph_miraflores_v2.json";
    std::cout << "============================================\n";
    std::cout << " Soft Heap — Dijkstra (1→todos) & Prim (MST)\n";
    std::cout << "============================================\n\n";

    GeoGraph g;
    try { g = load_osmnx_json(json_file); }
    catch (std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n"; return 1;
    }
    if (g.n==0) { std::cerr << "Grafo vacio\n"; return 1; }

    int src = 0;
    std::cout << "Origen (Dijkstra): nodo " << src;
    if (src<(int)g.vertices.size())
        std::cout << "  lat=" << g.vertices[src].lat
                  << " lon=" << g.vertices[src].lon;
    std::cout << "\n\n";

    // ----------------------------------------------------------
    // DIJKSTRA: 1 nodo → TODOS los demas
    // ----------------------------------------------------------
    std::cout << "--- DIJKSTRA desde nodo " << src << " ---\n";
    std::cout << "(calcula distancia minima a los " << g.n-1
              << " nodos restantes)\n";

    auto rd_dist  = dijkstra_std(g.adj_dist, g.n, src);
    auto rsh_dist = dijkstra_sh (g.adj_dist, g.n, src, 0.1);
    auto rd_time  = dijkstra_std(g.adj_time, g.n, src);
    auto rsh_time = dijkstra_sh (g.adj_time, g.n, src, 0.1);

    // Estadisticas sobre todos los nodos alcanzados
    int reachable = 0;
    double max_dist = 0, sum_dist = 0;
    for (int i=0; i<g.n; i++) {
        if (rd_dist.dist[i]<1e15) {
            reachable++;
            sum_dist += rd_dist.dist[i];
            max_dist  = std::max(max_dist, rd_dist.dist[i]);
        }
    }
    std::cout << "  Exacto    nodos alcanzados=" << reachable
              << "  dist_max=" << std::fixed << std::setprecision(1)
              << max_dist << "m"
              << "  dist_avg=" << (reachable>0?sum_dist/reachable:0)
              << "m  (" << rd_dist.ms << "ms)\n";

    // Error del Soft Heap sobre todos los nodos
    double err_sum=0; int err_cnt=0;
    for (int i=0; i<g.n; i++) {
        if (rd_dist.dist[i]<1e15 && rsh_dist.dist[i]<1e15) {
            err_sum += std::abs(rsh_dist.dist[i]-rd_dist.dist[i]);
            err_cnt++;
        }
    }
    std::cout << "  SH e=0.1  error_promedio="
              << (err_cnt>0?err_sum/err_cnt:0) << "m"
              << "  (" << rsh_dist.ms << "ms)\n";

    std::cout << "\n  Por tiempo:\n";
    std::cout << "  Exacto    (" << rd_time.ms << "ms)\n";
    std::cout << "  SH e=0.1  (" << rsh_time.ms << "ms)\n";

    // ----------------------------------------------------------
    // PRIM: MST que conecta TODOS los nodos
    // ----------------------------------------------------------
    std::cout << "\n--- PRIM MST (conecta los " << g.n
              << " nodos) ---\n";
    std::cout << "(usa " << g.n-1 << " aristas para conectar todo)\n";

    auto pm_dist   = prim_std(g.adj_dist, g.n);
    auto pmsh_dist = prim_sh (g.adj_dist, g.n, 0.1);
    auto pm_time   = prim_std(g.adj_time, g.n);
    auto pmsh_time = prim_sh (g.adj_time, g.n, 0.1);

    std::cout << "  Por distancia:\n";
    std::cout << "    Exacto    total=" << std::fixed
              << std::setprecision(1) << pm_dist.total_w << "m"
              << "  aristas=" << parent_to_edges(pm_dist.parent).size()
              << "  (" << pm_dist.ms << "ms)\n";
    double err_prim = std::abs(pmsh_dist.total_w-pm_dist.total_w);
    std::cout << "    SH e=0.1  total=" << pmsh_dist.total_w << "m"
              << "  error=" << err_prim << "m"
              << "  (" << pmsh_dist.ms << "ms)\n";

    std::cout << "  Por tiempo:\n";
    std::cout << "    Exacto    total=" << pm_time.total_w << "s"
              << "  (" << pm_time.ms << "ms)\n";
    std::cout << "    SH e=0.1  total=" << pmsh_time.total_w << "s"
              << "  error=" << std::abs(pmsh_time.total_w-pm_time.total_w)
              << "s  (" << pmsh_time.ms << "ms)\n";

    // ----------------------------------------------------------
    // GUARDAR JSON
    // ----------------------------------------------------------
    save_results_json(
        g,
        rd_dist, rsh_dist,
        rd_time, rsh_time,
        pm_dist, pmsh_dist,
        pm_time, pmsh_time,
        src,
        "resultados_miraflores.json"
    );

    return 0;
}