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
class BTreeBase : public dbindex<Key, Value> {
public:
    using Visitor = typename dbindex<Key, Value>::Visitor;
    explicit BTreeBase(int order, Compare cmp = Compare{}) 
    : min_deg((order + 1) / 2), cmp_(cmp) { // change the order from fan-out degree to CLRD definition.
        if(order < 3) {
            throw std::invalid_argument("order must be >= 3");
        }  
    }
    ~BTreeBase() override { destroy(root); }

    int order() const { 
        return 2 * min_deg; 
    }
    int min_degree() const { 
        return min_deg; 
    }

    bool insert(const Key& key, const Value& value) override {
        if(root == NULL) {
            root = new Node(true);
            root->keys.push_back(key);
            root->values.push_back(value);
            size_++;
            return true;
        }
        if((int)root->keys.size() == max_keys()) {
            root_split();
        }
        return insert_non_full(root, key, value);
    }

    const Value* find(const Key& key) const override {
        Node* node = root;
        while(node != NULL) {
            int i = find_key(node, key);
            if(i < (int)node->keys.size() && eq(node->keys[i], key)) {
                return &node->values[i];
            }
            if(node->is_leaf) {
                return NULL;
            }
            node = node->children[i];
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
        if(root->keys.empty()) {
            Node* old = root;
            root = old->is_leaf ? NULL : old->children[0];
            delete old;
        }
        if(flag) {
            size_--;
        }
        return flag;
    }

    void range(const Key& low, const Key& high, const Visitor& visit) const override {
        if(root == NULL || cmp_(high, low)) {
            return;
        }
        range_recur(root, low, high, visit);
    }
    std::size_t size() const override { 
        return size_; 
    }

    bool empty() const override { 
        return size_ == 0; 
    }

    bool check() const override {
        if(root == NULL) {
            return empty();
        }
        std::size_t counted = 0;
        int leaf_depth = -1;
        bool flag = check_node(root, 0, true, NULL, NULL, leaf_depth, counted);
        return flag && counted == size();
    }

    int height() const override {
        if(root == NULL) {
            return -1;
        }
        int h = 0;
        Node* n = root;
        while(!n->is_leaf) {
            n = n->children[0];
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
        for(Node* c : root->children) {
            collect_fill(c, total_keys, node_count);
        }
        if(node_count == 0) {
            return 0.0;
        }
        return (double)total_keys / ((double)node_count * (double)max_keys());
    }

    std::size_t total_splits() const override { 
        return split_count_; 
    }
    std::size_t total_redistributes() const override { 
        return 0; 
    }


protected:
    struct Node {
        bool is_leaf;
        std::vector<Key> keys;
        std::vector<Value> values;
        std::vector<Node*> children;
        explicit Node(bool leaf_flag) 
        : is_leaf(leaf_flag) {}
    };

    int min_deg;
    Node* root = NULL;
    std::size_t size_ = 0;
    Compare cmp_;
    std::size_t split_count_ = 0;

    virtual int max_keys() const { 
        return 2 * min_deg - 1; 
    }
    virtual int min_keys() const { 
        return min_deg - 1; 
    }

    bool eq(const Key& a, const Key& b) const {
        return !cmp_(a, b) && !cmp_(b, a);
    }
    int find_key(Node* node, const Key& key) const {
        int i = 0;
        int n = (int)node->keys.size();
        while(i < n && cmp_(node->keys[i], key)) {
            i++;
        }
        return i;
    }

    virtual void root_split() {
        Node* new_root = new Node(false);
        new_root->children.push_back(root);
        do_split(new_root, 0);
        root = new_root;
    }

    bool insert_non_full(Node* node, const Key& key, const Value& value) {
        int idx = find_key(node, key);
        if(idx < (int)node->keys.size() && eq(node->keys[idx], key)) {
            node->values[idx] = value;
            return false;
        }
        if(node->is_leaf) {
            node->keys.insert(  node->keys.begin() + idx, key);
            node->values.insert(node->values.begin() + idx, value);
            size_++;
            return true;
        }
        if((int)node->children[idx]->keys.size() == max_keys()) {
            do_split(node, idx);
            int probe = find_key(node, key);
            if(probe < (int)node->keys.size() && eq(node->keys[probe], key)) {
                node->values[probe] = value;
                return false;
            }
            idx = probe;
        }
        return insert_non_full(node->children[idx], key, value);
    }

    bool do_erase(Node* node, const Key& key) {
        int idx = find_key(node, key);
        if(idx < (int)node->keys.size() && eq(node->keys[idx], key)) {
            if(node->is_leaf) {
                node->keys.erase(node->keys.begin() + idx);
                node->values.erase(node->values.begin() + idx);
            } 
            else {
                erase_from_internal(node, idx);
            }
            return true;
        }
        if(node->is_leaf) {
            return false;
        }
        const bool last_flag = (idx == (int)node->keys.size());
        if((int)node->children[idx]->keys.size() <= min_keys()) {
            do_fill(node, idx);
        }
        if(last_flag && idx > (int)node->keys.size()) {
            return do_erase(node->children[idx - 1], key);
        }
        return do_erase(node->children[idx], key);
    }

    void erase_from_internal(Node* node, int idx) {
        Node* L = node->children[idx];
        Node* R = node->children[idx + 1];
        if((int)L->keys.size() > min_keys()) {
            auto [pred_key, pred_value] = get_predecessor(node, idx);
            node->keys[idx] = pred_key;
            node->values[idx] = pred_value;
            do_erase(L, pred_key);
        } 
        else if((int)R->keys.size() > min_keys()) {
            auto [succ_key, succ_value] = get_successor(node, idx);
            node->keys[idx] = succ_key;
            node->values[idx] = succ_value;
            do_erase(R, succ_key);
        } 
        else {
            Key key = node->keys[idx];
            merge_two(node, idx);
            do_erase(node->children[idx], key);
        }
    }

    std::pair<Key, Value> get_predecessor(Node* node, int idx) {
        Node* cur = node->children[idx];
        while(!cur->is_leaf) {
            cur = cur->children.back();
        }
        return {cur->keys.back(), cur->values.back()};
    }

    std::pair<Key, Value> get_successor(Node* node, int idx) {
        Node* cur = node->children[idx + 1];
        while(!cur->is_leaf) {
            cur = cur->children.front();
        }
        return {cur->keys.front(), cur->values.front()};
    }

    virtual void do_split(Node* parent, int i) = 0;

    virtual void do_fill(Node* parent, int i) = 0;

    void split_in_two(Node* parent, int i) {
        split_count_++;
        Node* full_node = parent->children[i];
        Node* sibling = new Node(full_node->is_leaf);
        sibling->keys.assign(std::make_move_iterator(full_node->keys.begin() + min_deg), std::make_move_iterator(full_node->keys.end()));
        sibling->values.assign(std::make_move_iterator(full_node->values.begin() + min_deg), std::make_move_iterator(full_node->values.end()));
        if(!full_node->is_leaf) {
            sibling->children.assign(full_node->children.begin() + min_deg, full_node->children.end());
            full_node->children.resize(min_deg);
        }
        Key mid_k = std::move(full_node->keys[min_deg - 1]);
        Value mid_v = std::move(full_node->values[min_deg - 1]);
        full_node->keys.resize(min_deg - 1);
        full_node->values.resize(min_deg - 1);
        parent->children.insert(parent->children.begin() + i + 1, sibling);
        parent->keys.insert(parent->keys.begin() + i, std::move(mid_k));
        parent->values.insert(parent->values.begin() + i, std::move(mid_v));
    }

    void rotate_right(Node* parent, int i) {
        Node* child = parent->children[i];
        Node* sibling = parent->children[i - 1];
        child->keys.insert(child->keys.begin(), parent->keys[i - 1]);
        child->values.insert(child->values.begin(), parent->values[i - 1]);
        if(!child->is_leaf) {
            child->children.insert(child->children.begin(), sibling->children.back());
            sibling->children.pop_back();
        }
        parent->keys[i - 1] = sibling->keys.back();
        parent->values[i - 1] = sibling->values.back();
        sibling->keys.pop_back();
        sibling->values.pop_back();
    }

    void rotate_left(Node* parent, int i) {
        Node* child = parent->children[i];
        Node* sibling = parent->children[i + 1];
        child->keys.push_back(parent->keys[i]);
        child->values.push_back(parent->values[i]);
        if(!child->is_leaf) {
            child->children.push_back(sibling->children.front());
            sibling->children.erase(sibling->children.begin());
        }
        parent->keys[i] = sibling->keys.front();
        parent->values[i] = sibling->values.front();
        sibling->keys.erase(sibling->keys.begin());
        sibling->values.erase(sibling->values.begin());
    }

    void merge_two(Node* parent, int i) {
        Node* L = parent->children[i];
        Node* R = parent->children[i + 1];
        L->keys.push_back(std::move(parent->keys[i]));
        L->values.push_back(std::move(parent->values[i]));
        for(auto& k : R->keys) {
            L->keys.push_back(std::move(k));
        }   
        for(auto& v : R->values) {
            L->values.push_back(std::move(v));
        }
        if(!L->is_leaf) {
            for(Node* c : R->children) {
                L->children.push_back(c);
            }
        }
        parent->keys.erase(parent->keys.begin() + i);
        parent->values.erase(parent->values.begin() + i);
        parent->children.erase(parent->children.begin() + i + 1);
        delete R;
    }

    void range_recur(Node* node, const Key& low, const Key& high, const Visitor& visit) const {
        const int n = (int)node->keys.size();
        int i = 0;
        while(i < n && cmp_(node->keys[i], low)) {
            i++;
        }
        while(i < n && !cmp_(high, node->keys[i])) {
            if (!node->is_leaf) range_recur(node->children[i], low, high, visit);
            visit(node->keys[i], node->values[i]);
            i++;
        }
        if(!node->is_leaf) {
            range_recur(node->children[i], low, high, visit);
        }
    }

    bool check_node(Node* node, int depth, bool is_root, const Key* low_bound, const Key* high_bound, int& leaf_depth, std::size_t& counted) const {
        const int n = (int)node->keys.size();
        if(n == 0 && !is_root) {
            return false;
        }
        if(n > max_keys()) {
            return false;
        }
        if(!is_root && n < min_deg - 1) {
            return false;
        }
        for (int i = 1; i < n; i++) {
            if(!cmp_(node->keys[i - 1], node->keys[i])) {
                return false;
            }
        }
        if(n > 0) {
            if(low_bound && !cmp_(*low_bound, node->keys[0])) {
                return false;
            }
            if(high_bound && !cmp_(node->keys[n - 1], *high_bound)) {
                return false;
            }
        }
        counted += (std::size_t)n;
        if(node->is_leaf) {
            if (leaf_depth == -1) leaf_depth = depth;
            else if(leaf_depth != depth) {
                return false;
            }
            return true;
        }
        if((int)node->children.size() != n + 1) {
            return false;
        }

        for(int i = 0; i <= n; i++) {
            const Key* lo = (i == 0) ? low_bound : &node->keys[i - 1];
            const Key* hi = (i == n) ? high_bound : &node->keys[i];
            if(!check_node(node->children[i], depth + 1, false, lo, hi, leaf_depth, counted)) {
                return false;
            }
        }
        return true;
    }
    void destroy(Node* node) {
        if(node == NULL) {
            return;
        }
        if(!node->is_leaf) {
            for(Node* c : node->children) {
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
            for(Node* c : node->children) {
                collect_fill(c, total_keys, node_count);
            }
        }
    }
};

template <typename Key, typename Value, typename Compare = std::less<Key>>
class BTree : public BTreeBase<Key, Value, Compare> {
protected:
    using Base = BTreeBase<Key, Value, Compare>;
    using Node = typename Base::Node;
    using Base::min_keys;
    using Base::split_in_two;
    using Base::rotate_left;
    using Base::rotate_right;
    using Base::merge_two;

public:
    using Base::Base;

protected:
    void do_split(Node* parent, int i) override { 
        split_in_two(parent, i); 
    }
    void do_fill(Node* parent, int i) override {
        const int n = (int)parent->keys.size();
        if(i > 0 && (int)parent->children[i - 1]->keys.size() > min_keys()) {
            rotate_right(parent, i);
        } 
        else if(i < n && (int)parent->children[i + 1]->keys.size() > min_keys()) {
            rotate_left(parent, i);
        } 
        else {
            if(i < n) {
                merge_two(parent, i);
            }
            else {
                merge_two(parent, i - 1);
            }
        }
    }
};