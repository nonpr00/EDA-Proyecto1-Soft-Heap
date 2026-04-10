#ifndef SOFT_HEAP_GENERIC_H
#define SOFT_HEAP_GENERIC_H


#include <cmath>
#include <limits>
#include <stdexcept>
#include <cassert>
#include <iostream>
#include <utility>


using namespace std;


template <typename T>
class SoftHeap {


    struct Cell {
        /*
         * esto representa item de una lsita enlasada, si intanciara en un nodo
         * aca se guarda las claves originales y su dato asociado
         */
        double key;
        T      data;
        Cell*  next;

        Cell(double k, T d) : key(k), data(d), next(nullptr) {}
    };


    struct Node {
        /*
         * esto represnta los nodos del arbol binario
         * gurda el inicio y el final de la lista para tener operaciones mas rapidas
         * aca se gurada al clave corrupta (ckey) que es max( de los cells)
         * tambien el tamaño objetivo de la lista de cells, el rank que represneta el nodo,
         * el numero de elementos actules(cells)
         */
        Node*  left;
        Node*  right;
        Cell*  first;
        Cell*  last;
        double ckey;
        int    rank;
        int    size;
        int    nelems;

        Node(double key, int sz)
            : left(nullptr), right(nullptr),
              first(nullptr), last(nullptr),
              ckey(key), rank(0), size(sz), nelems(0) {}
    };


    struct Tree {
        /*
         * es un item de una lsita doblemenete enlsada, ademas gurada el tree de minimo ckey hasta ese tree
         * guarda el nodo raiz como cualquier tree
         * y rank guarda el rank del nodo raiz
         */
        Tree* prev;
        Tree* next;
        Tree* sufmin;
        Node* root;
        int   rank;

        Tree() : prev(nullptr), next(nullptr),
                 sufmin(nullptr), root(nullptr), rank(0) {}
    };

    /*
     * aca first gurda el inicio de la lista de trees
     * top_rank_ el rank maximo entre los trees
     * epsilon_ la constante para determinar r
     * r_ la contante que nos indica desde que nodos con rank puede hacer aplicar a  corrupcion
     */
    Tree*  first_;
    int    top_rank_;
    double epsilon_;
    int    r_;

    int get_r(double eps) {
        /*
         * determina la r dependiendo donde rank >r pueden ser corruptos
         */
        return (int) ceil(log2(1.0 / eps)) + 5;
    }

    int get_size(int rank, int prev_size, int r_param) {
        /*
         * dado el rank del nodo y el tamaño objetivo del node de rank previo(size) y la constante r,
         * calculmos el tamaño objetivo que puede tener nuestro nodo con rank (k)
         * si el rank es menor al r , el nodo solo puede tener 1 elemento en su lista
         * cc: el nodo puede tener techo(3/2*tamaño_objetivo del nodo con rank previo)
         */
        if (rank <= r_param) return 1;
        return (3 * prev_size + 1) / 2;
    }

    bool is_leaf(Node* x){
        /*
         *verifica si el nodo es hoja
         */
        return !x->left && !x->right;
    }

    void push_item(Node* x, double k, const T& d) {
        /*
         * agregamos un nuevo elemento al final de la lista
         * actulizamos la direecion del final de la lista
         * actulizamos el numero de elementos
         *
         */
        Cell* c = new Cell(k, d);
        if (!x->last) x->first = x->last = c;
        else          { x->last->next = c; x->last = c; }
        x->nelems++;
    }


    pair<double, T> pop_item(Node* x) {
        /*
         * saca el primer de la lista el nodo
         * actualiza el primer item y el ultimo de ser nesesario de la lista
         * reduce el numero de elementos
         */
        assert(x->first);
        Cell* old = x->first;
        auto  res = make_pair(old->key, old->data);
        x->first  = old->next;
        if (!x->first) x->last = nullptr;
        delete old;
        x->nelems--;
        return res;
    }


    void move_list(Node* src, Node* dst) {
        /*
         * mueve la lista de un nodo src hacia el final un nodo de destino (dst)
         * aumenta el numero de elementos del nodo dst
         * deja vacia la lista del nodo src y reduce el numero de elementosha 0
         */
        if (!src->first) return;
        if (dst->last) dst->last->next = src->first;
        else           dst->first = src->first;
        dst->last    = src->last;
        dst->nelems += src->nelems;
        src->first = src->last = nullptr;
        src->nelems = 0;
    }


    void sift(Node* x) {

        /*
         *Iteramos si numero de elementos en el nodo actual es menor al numero
         *de elementos objetivo(size)
         *promete: numeros de elementos debe alcansar almenos el objetivo de size sino convertimos en hoja
        */
        while (x->nelems < x->size && !is_leaf(x)) {
            /*
             * no aseguramos que el hijo con menor ckey sea el hijo izquierdo
             */
            if (!x->left ||
               (x->right && x->left->ckey > x->right->ckey))
                swap(x->left, x->right);
            /*
             * movemos la lista de valores del hijo menor al nodo padre
             * aca ocurre la corrupcion
             * el padre aumenta su ckey al del hijo
             * lista del nodo hijo (izq) queda vacia
             */
            move_list(x->left, x);
            x->ckey = x->left->ckey;

            /*
             * si es es hoja: el nodo izq(menor ckey) lo eleminamos
             * cc: hacemos el mismo procesos para el hijo izq
             */
            if (is_leaf(x->left)) {
                free_node(x->left);
                x->left = nullptr;
            } else {
                sift(x->left);
            }
        }
    }


    Node* combine(Node* x, Node* y) {
        /*
         * combina 2 nodos del miso rank
         * aumenta el rnk del nodo
         * calcula su tamaño objetivo dado el nuevo rank , tamaño objetivo de los nodos a fusionarse
         * crea un nuevo nodo con key 0 y nuevo tamaño objetivo, pero ningun elemento en su lista
         * asigna los nodos a combinar  a los nodos hijos (no nesariamnete de forma ordenada)
         * asigna su rank
         * el punto importante es cuando aplica sift al nuevo nodo donde el sift grantiza que
         * tener almenos 1 elemento  pero menor al numero de elementos objetivos,
         * y que el z->ckey queda con el minimo de los 2 hijos
         * en este caso para el uevo nod resultado de la combinacion
         */
        int new_rank = x->rank + 1;
        int new_size = get_size(new_rank, x->size, r_);
        Node* z = new Node(0.0, new_size);
        z->left  = x;
        z->right = y;
        z->rank  = new_rank;
        sift(z);
        return z;
    }

    void insert_tree(Tree* ins, Tree* succ) {
        /*
         *insret un tree justo antes de succ
         *si es que previo de succ es nulo entonces first_ toma el valor de ins
         *cc: el previo actualiza el puntero de next a inst
         *al final actualiza los punteros y deja la lista de forma correcta
         */
        ins->next = succ;
        if (!succ->prev) first_ = ins;
        else succ->prev->next = ins;
        ins->prev  = succ->prev;
        succ->prev = ins;
    }

    void remove_tree(Tree* t) {
        /*
         * remueve el t
         * actualiza el first_ del heap de ser nesesario
         */
        if (!t->prev) first_ = t->next;
        else          t->prev->next = t->next;
        if (t->next)  t->next->prev = t->prev;
    }

    void update_sufmin(Tree* cur) {
        /*
         * actualiza el tree con sufmin, recorriendo desde current hacia atras
         */
        while (cur) {
            if (!cur->next ||
                cur->root->ckey <= cur->next->sufmin->root->ckey)
                cur->sufmin = cur;
            else
                cur->sufmin = cur->next->sufmin;
            cur = cur->prev;
        }
    }

    void merge_into(SoftHeap* P, SoftHeap* Q) {
        /*
         *la funcion asume que las lista de p y q estan ordenads por rank
         *recorre la lista de tree de P y lo va insertandando cada tree en rank<= al rank de Q
         *evuelve la lista de trees mergeados y ordendos en Q y lista de trees vacia en P
         */
        Tree* cp = P->first_;
        Tree* cq = Q->first_;
        while (cp) {
            int rank_cp = cp->rank;
            int rank_cq = cq->rank;
            while (cq && rank_cq < rank_cp ) cq = cq->next;
            Tree* nxt = cp->next;
            Q->insert_tree(cp, cq);
            cp = nxt;
        }
    }

    void repeated_combine(SoftHeap* Q, int smaller_rank) {
        /*
         * revisa hasta un smalller maximo , si es que hay colisiones
         * en el caso de no aver colisiones avanza al siguiente verifcando
         * que rank actual no supere a smaller_rank
         * en el caso de aver colision y no 3 seguidas: combina lso 2 con colisones
         * y actuliza el rank y las lsta de treess
         * en caso de 3 colisones avanza a la siguiente y combin las 2 siguientes
         * al final actualizamos en rank si es que en las combinaciones aumento y
         * y actulizamos el minimo hasta donde hemos cambinado
         */
        Tree* curr = Q->first_;
        while (curr->next) {
            bool two   = curr->rank == curr->next->rank;
            bool three = two && curr->next->next &&
                         curr->rank == curr->next->next->rank;
            if (!two) {
                if (curr->rank > smaller_rank) break;
                curr = curr->next;
            } else if (!three) {
                curr->root = combine(curr->root, curr->next->root);
                curr->rank = curr->root->rank;
                Tree* tf   = curr->next;
                Q->remove_tree(curr->next);
                delete tf;
            } else {
                curr = curr->next;
            }
        }
        if (curr->rank > Q->top_rank_) Q->top_rank_ = curr->rank;
        Q->update_sufmin(curr);
    }

    void free_node(Node* x) {
        /*elimin recursivamnete un no nodo*/
        if (!x) return;
        Cell* c = x->first;
        while (c) { Cell* n = c->next; delete c; c = n; }
        free_node(x->left);
        free_node(x->right);
        delete x;
    }

    void free_all() {
        /*
         * libera todo los trees del soft heap, y deja la lista vacia
         */
        Tree* t = first_;
        while (t) {
            Tree* n = t->next;
            free_node(t->root);
            delete t;
            t = n;
        }
        first_ = nullptr;
    }

    Tree* make_tree(double key, const T& data) {
        Node* nd = new Node(key, 1);
        push_item(nd, key, data);
        Tree* t   = new Tree();
        t->root   = nd;
        t->rank   = 0;
        t->sufmin = t;
        return t;
    }

public:

    explicit SoftHeap(double eps = 0.5)
        : first_(nullptr), top_rank_(-1), epsilon_(eps)
    {
        if (eps <= 0 || eps > 0.5)
            throw invalid_argument("epsilon debe estar entre 0 y 0.5");
        r_ = get_r(eps);
    }

    ~SoftHeap() { free_all(); }


    void insert(double key, const T& data) {
        /*
         * insertamo un nuevo key con su data
         * creamos el tree , si es el primero solo actualizamos first
         * cc: creamos un soft heap temporal de rank k , y llamaon a meld
         * meld se encargara de comibinas hambos soft heaps y actulizar el minimo de los trees
         */
        Tree* t = make_tree(key, data);
        if (!first_) {
            first_    = t;
            top_rank_ = 0;
        } else {
            SoftHeap tmp(epsilon_);
            tmp.first_    = t;
            tmp.top_rank_ = 0;
            tmp.r_        = r_;
            meld(tmp);
        }
    }

    double find_min_key() {
        /*
         * devuelve el valor minimmo ckey del tree minimo , puede estar corrupto y no ser el minimo
         */
        if (!first_) throw runtime_error("heap esta vacio");
        return first_->sufmin->root->ckey;
    }

    T find_min_data() {
        /*
         * retrna el primer item de la lista del node del tree con de menor ckey en su root
         *
         */
        if (!first_) throw runtime_error("heap esta vacio");
        return first_->sufmin->root->first->data;
    }


    pair<double, T> delete_min() {
        /*
         * elimina el primer item del nodo root, de tree minimo
         * si el numero de elementos es menor e igual a piso de ( el tañamo objetivo /2)
         * actualizamos Tmin sino es hoja con sift
         * y de ser hoja y no tener elementos la removemos y,  actualizamos el rank top y el tree minimo de los trees anteriores de ser nesesario
         */
        if (!first_) throw runtime_error("heap esta vacio");

        Tree* Tmin = first_->sufmin;
        Node* x  = Tmin->root;

        auto result = pop_item(x);

        if (x->nelems <= x->size / 2) {
            if (!is_leaf(x)) {
                sift(x);
                update_sufmin(Tmin);
            } else if (x->nelems == 0) {
                free_node(x);
                Tmin->root = nullptr;
                remove_tree(Tmin);
                if (!Tmin->next)
                    top_rank_ = Tmin->prev ? Tmin->prev->rank : -1;
                if (Tmin->prev) update_sufmin(Tmin->prev);
                delete Tmin;
            }
        }
        return result;
    }

    bool is_empty() const {
        /*
         * verifica si es qeu no tiene tress
         */
        return first_ == nullptr;
    }

    void meld(SoftHeap& other) {
        /*
         * combina la lista de trees del soft heap actual cn otro soft heap
         * si alguno de ellos no tiene treres solo actualizaos la lsita de trees actuales de ser nesesario
         * CC: encontramos el rnak menor entre ambos y ordenamos la lsta de trees con merge into
         * quedan ordenadas de forma pero no combinadas
         * al final combinamos con repeated_combine hasta el menor rango encontrado entre ellos
         * el otro heap queda vcio
         */
        if (!other.first_) return;
        if (!first_) {
            first_    = other.first_;
            top_rank_ = other.top_rank_;
            other.first_    = nullptr;
            other.top_rank_ = -1;
            return;
        }
        int smaller_rank;
        if (top_rank_ >= other.top_rank_) {
            smaller_rank = other.top_rank_;
            merge_into(&other, this);
        } else {
            smaller_rank = top_rank_;
            merge_into(this, &other);
            first_    = other.first_;
            top_rank_ = other.top_rank_;
        }
        other.first_    = nullptr;
        other.top_rank_ = -1;
        repeated_combine(this, smaller_rank);
    }

    void print_state() {
        /*
         * para debugs imprime datos del soft heap y los trees mostrando su ckey
         */
        cout << "SoftHeap(eps=" << epsilon_
                  << " r=" << r_ << ")\n";
        if (!first_) {
            cout << "no hay trees\n";
            return;
        }
        for (Tree* t = first_; t; t = t->next) {
            cout << "rank=" << t->rank
            << " ckey=" << t->root->ckey
            << " num_elems=" << t->root->nelems << "\n";
        }
        cout << "  min_ckey=" << find_min_key() << "\n";
    }
};

#endif