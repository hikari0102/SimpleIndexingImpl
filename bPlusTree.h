#pragma once

#include "dbindex.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

template <typename Key, typename Value, typename Compare = std::less<Key>>
class BPlusTree : public dbindex<Key, Value> {
public: 
    using Visitor = typename dbindex<Key, Value>::Visitor; 
    explicit BPlusTree(int order, Compare cmp = Compare{})
    : min_deg((order + 1) / 2), cmp_(cmp) {
        if(order < 3) {
            throw std::invalid_argument("order must be >= 3");
        }
    }

    int order() const { 
        return 2 * min_deg; 
    }
    int min_degree() const { 
        return min_deg; 
    }
    ~BPlusTree() override { 
        destroy(root); 
    }
    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    bool insert(const Key& key, const Value& value) override {
        if(root == NULL) {
            LeafNode* leaf = new LeafNode();
            leaf->keys.push_back(key);
            leaf->values.push_back(value);
            root = leaf;
            leaf_head = leaf;
            size_++;
            return true;
        }
        if((int)root->keys.size() == max_keys()) {
            root_split();
        }
        return insert_non_full(root, key, value);
    }

    const Value* find(const Key& key) const override {
        LeafNode* leaf = find_leaf(key);
        if(leaf == NULL) {
            return NULL;
        }
        int i = lower_bound_idx(leaf, key);
        if(i < (int)leaf->keys.size() && eq(leaf->keys[i], key)) {
            return &leaf->values[i];
        }  
        return NULL;
    }
    bool contains(const Key& key) const override { 
        return find(key) != NULL; 
    }

    bool erase(const Key& key) override {
        if(root == NULL) {
            return false;
        }
        bool flag = do_erase(root, key);
        if(!root->is_leaf && root->keys.empty()) {
            InternalNode* old = as_internal(root);
            root = old->children[0];
            old->children.clear();
            delete old;
        } 
        else if(root->is_leaf && root->keys.empty()) {
            delete root;
            root = NULL;
            leaf_head = NULL;
        }
        if(flag) {
            size_--;
        }
        return flag;
    }

    void range(const Key& low, const Key& high, const Visitor& visit) const override {
        if (root == NULL || cmp_(high, low)) return;
        LeafNode* leaf = find_leaf(low);
        if(leaf == NULL) {
            return;
        }
        int i = lower_bound_idx(leaf, low);
        while(leaf != NULL) {
            const int n = (int)leaf->keys.size();
            for(; i < n; i++) {
                if(cmp_(high, leaf->keys[i])) {
                    return;
                }
                visit(leaf->keys[i], leaf->values[i]);
            }
            leaf = leaf->next;
            i = 0;
        }
    }

    std::size_t size() const override { 
        return size_; 
    }
    bool empty() const override { 
        return size_ == 0; 
    }

    bool check() const override {
        if(root == NULL) {
            return size_ == 0 && leaf_head == NULL;
        }
        std::size_t leaf_count = 0;
        int leaf_depth = -1;
        if(!check_node(root, 0, true, NULL, NULL, leaf_depth, leaf_count))
            return false;
        if(leaf_count != size_) {
            return false;
        }
        std::size_t walked = 0;
        LeafNode* prev = NULL;
        for(LeafNode* l = leaf_head; l; l = l->next) {
            if (l->prev != prev) {
                return false;
            }
            for(std::size_t i = 1; i < l->keys.size(); i++) {
                if(!cmp_(l->keys[i - 1], l->keys[i])) {
                    return false;
                }
            }
            if(prev && !prev->keys.empty() && !l->keys.empty()) {
                if(!cmp_(prev->keys.back(), l->keys.front())) {
                    return false;
                }
            }
            walked += l->keys.size();
            prev = l;
        }
        return walked == size_;
    }

    int height() const override {
        if(root == NULL) {
            return -1;
        }
        int h = 0;
        Node* n = root;
        while(!n->is_leaf) {
            n = as_internal(n)->children[0];
            h++;
        }
        return h;
    }

    double fill_factor() const override {
        if(root == NULL || root->is_leaf) {
            return 0.0;
        }
        std::size_t total_keys = 0;
        std::size_t node_count = 0;
        for(Node* c : as_internal(root)->children) {
            collect_fill(c, total_keys, node_count);
        }
        if(node_count == 0) {
            return 0.0;
        }
        return (double)total_keys / ((double)node_count * (double)max_keys());
    }

private:
    struct Node {
        bool is_leaf;
        std::vector<Key> keys;
        explicit Node(bool leaf_flag) : is_leaf(leaf_flag) {}
        virtual ~Node() = default;
    };
    struct InternalNode : Node {
        std::vector<Node*> children;
        InternalNode() 
        : Node(false) {}
    };
    struct LeafNode : Node {
        std::vector<Value> values;
        LeafNode* prev = NULL;
        LeafNode* next = NULL;
        LeafNode() 
        : Node(true) {}
    };
    static InternalNode* as_internal(Node* n) {
        return static_cast<InternalNode*>(n);
    }
    static LeafNode* as_leaf(Node* n) {
        return static_cast<LeafNode*>(n);
    }

    int max_keys() const { 
        return 2 * min_deg - 1; 
    }
    int min_keys() const { 
        return min_deg - 1; 
    }
    
    int min_deg;
    Node* root = NULL;
    LeafNode* leaf_head = NULL;
    std::size_t size_ = 0;
    Compare cmp_;
    bool eq(const Key& a, const Key& b) const {
        return !cmp_(a, b) && !cmp_(b, a);
    }
    int lower_bound_idx(Node* node, const Key& key) const {
        int i = 0;
        int n = (int)node->keys.size();
        while(i < n && cmp_(node->keys[i], key)) {
            i++;
        }
        return i;
    }

    LeafNode* find_leaf(const Key& key) const {
        Node* node = root;
        while(node && !node->is_leaf) {
            int i = 0;
            int n = (int)node->keys.size();
            while(i < n && !cmp_(key, node->keys[i])) {
                i++;
            }
            node = as_internal(node)->children[i];
        }
        return as_leaf(node);
    }

    void root_split() {
        InternalNode* new_root = new InternalNode();
        new_root->children.push_back(root);
        split_child(new_root, 0);
        root = new_root;
    }
    
    void split_child(InternalNode* parent, int i) {
        Node* full = parent->children[i];
        if(full->is_leaf) {
            split_leaf_child(parent, i);
        }
        else {
            split_internal_child(parent, i);
        }
    }
    void split_leaf_child(InternalNode* parent, int i) {
        LeafNode* full_node = as_leaf(parent->children[i]);
        LeafNode* sibling = new LeafNode();
        sibling->keys.assign(std::make_move_iterator(full_node->keys.begin() + min_deg), std::make_move_iterator(full_node->keys.end()));
        sibling->values.assign(std::make_move_iterator(full_node->values.begin() + min_deg), std::make_move_iterator(full_node->values.end()));
        full_node->keys.resize(min_deg);
        full_node->values.resize(min_deg);
        sibling->next = full_node->next;
        sibling->prev = full_node;
        if(full_node->next != NULL) {
            full_node->next->prev = sibling;
        }
        full_node->next = sibling;

        Key router = sibling->keys.front();
        parent->children.insert(parent->children.begin() + i + 1, sibling);
        parent->keys.insert(parent->keys.begin() + i, std::move(router));
    }

    void split_internal_child(InternalNode* parent, int i) {
        InternalNode* full_node = as_internal(parent->children[i]);
        InternalNode* sibling = new InternalNode();
        sibling->keys.assign(std::make_move_iterator(full_node->keys.begin() + min_deg), std::make_move_iterator(full_node->keys.end()));
        sibling->children.assign(full_node->children.begin() + min_deg, full_node->children.end());
        Key middle = std::move(full_node->keys[min_deg - 1]);
        full_node->keys.resize(min_deg - 1);
        full_node->children.resize(min_deg);
        parent->children.insert(parent->children.begin() + i + 1, sibling);
        parent->keys.insert(parent->keys.begin() + i, std::move(middle));
    }

    bool insert_non_full(Node* node, const Key& key, const Value& value) {
        if(node->is_leaf) {
            LeafNode* leaf = as_leaf(node);
            int idx = lower_bound_idx(leaf, key);
            if(idx < (int)leaf->keys.size() && eq(leaf->keys[idx], key)) {
                leaf->values[idx] = value;
                return false;
            }
            leaf->keys.insert(leaf->keys.begin() + idx, key);
            leaf->values.insert(leaf->values.begin() + idx, value);
            size_++;
            return true;
        }
        InternalNode* in = as_internal(node);
        int idx = 0;
        int n = (int)in->keys.size();
        while(idx < n && !cmp_(key, in->keys[idx])) {
            idx++;
        }
        if((int)in->children[idx]->keys.size() == 2 * min_deg - 1) {
            split_child(in, idx);
            if(!cmp_(key, in->keys[idx])) {
                idx++;
            }
        }
        return insert_non_full(in->children[idx], key, value);
    }

    bool do_erase(Node* node, const Key& key) {
        if(node->is_leaf) {
            LeafNode* leaf = as_leaf(node);
            int idx = lower_bound_idx(leaf, key);
            if(idx < (int)leaf->keys.size() && eq(leaf->keys[idx], key)) {
                leaf->keys.erase(leaf->keys.begin() + idx);
                leaf->values.erase(leaf->values.begin() + idx);
                return true;
            }
            return false;
        }
        InternalNode* in = as_internal(node);
        int idx = 0, n = (int)in->keys.size();
        while(idx < n && !cmp_(key, in->keys[idx])) {
            idx++;
        }
        if((int)in->children[idx]->keys.size() < min_deg) {
            idx = fill_child(in, idx);
        }
        return do_erase(in->children[idx], key);
    }

    int fill_child(InternalNode* parent, int idx) {
        const int n = (int)parent->keys.size();
        if(idx > 0 && (int)parent->children[idx - 1]->keys.size() >= min_deg) {
            borrow_from_prev(parent, idx);
            return idx;
        }
        if(idx < n && (int)parent->children[idx + 1]->keys.size() >= min_deg) {
            borrow_from_next(parent, idx);
            return idx;
        }
        if(idx < n) { 
            merge_children(parent, idx);     
            return idx; 
        }
        else { 
            merge_children(parent, idx - 1); 
            return idx - 1; 
        }
    }

    void borrow_from_prev(InternalNode* parent, int idx) {
        Node* child   = parent->children[idx];
        Node* sibling = parent->children[idx - 1];
        if(child->is_leaf) {
            LeafNode* c = as_leaf(child);
            LeafNode* s = as_leaf(sibling);
            c->keys.insert(c->keys.begin(), std::move(s->keys.back()));
            c->values.insert(c->values.begin(), std::move(s->values.back()));
            s->keys.pop_back();
            s->values.pop_back();
            parent->keys[idx - 1] = c->keys.front();
        } 
        else {
            InternalNode* c = as_internal(child);
            InternalNode* s = as_internal(sibling);
            c->keys.insert(c->keys.begin(), std::move(parent->keys[idx - 1]));
            c->children.insert(c->children.begin(), s->children.back());
            s->children.pop_back();
            parent->keys[idx - 1] = std::move(s->keys.back());
            s->keys.pop_back();
        }
    }

    void borrow_from_next(InternalNode* parent, int idx) {
        Node* child = parent->children[idx];
        Node* sibling = parent->children[idx + 1];
        if(child->is_leaf) {
            LeafNode* c = as_leaf(child);
            LeafNode* s = as_leaf(sibling);
            c->keys.push_back(std::move(s->keys.front()));
            c->values.push_back(std::move(s->values.front()));
            s->keys.erase(s->keys.begin());
            s->values.erase(s->values.begin());
            parent->keys[idx] = s->keys.front();
        } 
        else {
            InternalNode* c = as_internal(child);
            InternalNode* s = as_internal(sibling);
            c->keys.push_back(std::move(parent->keys[idx]));
            c->children.push_back(s->children.front());
            s->children.erase(s->children.begin());
            parent->keys[idx] = std::move(s->keys.front());
            s->keys.erase(s->keys.begin());
        }
    }

    void merge_children(InternalNode* parent, int idx) {
        Node* left = parent->children[idx];
        Node* right = parent->children[idx + 1];
        if(left->is_leaf) {
            LeafNode* l = as_leaf(left);
            LeafNode* r = as_leaf(right);
            for(auto& k : r->keys) {
                l->keys.push_back(std::move(k));
            }
            for(auto& v : r->values) {
                l->values.push_back(std::move(v));
            }
            l->next = r->next;
            if(r->next != NULL) {
                r->next->prev = l;
            }
            parent->keys.erase(parent->keys.begin() + idx);
            parent->children.erase(parent->children.begin() + idx + 1);
            delete r;
        } 
        else {
            InternalNode* l = as_internal(left);
            InternalNode* r = as_internal(right);
            l->keys.push_back(std::move(parent->keys[idx]));
            for(auto& k : r->keys) {
                l->keys.push_back(std::move(k));
            }
            for(Node* c : r->children) {
                l->children.push_back(c);
            }
            parent->keys.erase(parent->keys.begin() + idx);
            parent->children.erase(parent->children.begin() + idx + 1);
            r->children.clear();
            delete r;
        }
    }

    bool check_node(Node* node, int depth, bool is_root, const Key* low_bound, const Key* high_bound, int& leaf_depth, std::size_t& leaf_count) const {
        const int n = (int)node->keys.size();
        if(!is_root && (n < min_keys() || n > max_keys())) {
            return false;
        }
        if(n > max_keys()) {
            return false;
        }
        for(int i = 1; i < n; i++) {
            if(!cmp_(node->keys[i - 1], node->keys[i])) {
                return false;
            }
        }
        if (n > 0) {
            if(low_bound != NULL && cmp_(node->keys[0], *low_bound)) {
                return false;
            }
            if (high_bound != NULL && cmp_(*high_bound, node->keys[n - 1])) {
                return false;
            }
        }
        if(node->is_leaf) {
            if (leaf_depth == -1) {
                leaf_depth = depth;
            }
            else if (leaf_depth != depth) {
                return false;
            }
            leaf_count += (std::size_t)n;
            return true;
        }
        InternalNode* in = (InternalNode*)node;
        if((int)in->children.size() != n + 1) {
            return false;
        }
        for(int i = 0; i <= n; i++) {
            const Key* lo = (i == 0) ? low_bound : &in->keys[i - 1];
            const Key* hi = (i == n) ? high_bound : &in->keys[i];
            if(!check_node(in->children[i], depth + 1, false, lo, hi, leaf_depth, leaf_count)) {
                return false;
            }
        }
        return true;
    }

    void destroy(Node* node) {
        if(node == NULL) return;
        if(!node->is_leaf) {
            for(Node* c : as_internal(node)->children) {
                destroy(c);
            }
        }
        delete node;
    }

    void collect_fill(Node* node, std::size_t& total_keys, std::size_t& node_count) const {
        if(node == NULL) {
            return;
        }
        total_keys += node->keys.size();
        node_count++;
        if(!node->is_leaf) {
            for(Node* c : as_internal(node)->children) {
                collect_fill(c, total_keys, node_count);
            }
        }
    }
};