#pragma once

#include "BTree.h"

#include <cstddef>
#include <iterator>
#include <utility>
#include <vector>

template <typename Key, typename Value, typename Compare = std::less<Key>>
class BStarTree : public BTree<Key, Value, Compare> {
    using Up = BTree<Key, Value, Compare>;
    using Base = BTreeBase<Key, Value, Compare>;
    using Node = typename Base::Node;
    using Base::min_deg;
    using Base::split_in_two;
    using Base::split_count_;

public:
    using Up::Up;

    std::size_t total_redistributes() const override { 
        return redist_count_; 
    }

protected:
    std::size_t redist_count_ = 0;
    void do_split(Node* parent, int i) override {
        const int n = (int)parent->keys.size();
        const int M = this->max_keys();
        if(i > 0 && (int)parent->children[i - 1]->keys.size() <= M - 2) {
            redistribute_to_left(parent, i);
            return;
        }
        if(i < n && (int)parent->children[i + 1]->keys.size() <= M - 2) {
            redistribute_to_right(parent, i);
            return;
        }
        if(i < n) {
            split_two_into_three(parent, i);
        }
        else if (i > 0) {
            split_two_into_three(parent, i - 1);
        }
        else {
            split_in_two(parent, i); 
        }
    }

    void redistribute_to_left(Node* parent, int i) {
        redist_count_++;
        Node* L = parent->children[i - 1];
        Node* R = parent->children[i];
        const int total = (int)L->keys.size() + 1 + (int)R->keys.size();
        const int new_L = total / 2;

        std::vector<Key> K; 
        K.reserve(total);
        std::vector<Value> V; 
        V.reserve(total);
        for(int j = 0; j < (int)L->keys.size(); j++) {
            K.push_back(std::move(L->keys[j]));
            V.push_back(std::move(L->values[j]));
        }
        K.push_back(std::move(parent->keys[i - 1]));
        V.push_back(std::move(parent->values[i - 1]));
        for(int j = 0; j < (int)R->keys.size(); j++) {
            K.push_back(std::move(R->keys[j]));
            V.push_back(std::move(R->values[j]));
        }
        std::vector<Node*> C;
        if(!L->is_leaf) {
            for (Node* c : L->children) C.push_back(c);
            for (Node* c : R->children) C.push_back(c);
        }

        L->keys.assign(std::make_move_iterator(K.begin()), std::make_move_iterator(K.begin() + new_L));
        L->values.assign(std::make_move_iterator(V.begin()), std::make_move_iterator(V.begin() + new_L));
        if(!L->is_leaf) {
            L->children.assign(C.begin(), C.begin() + new_L + 1);
        }

        parent->keys[i - 1] = std::move(K[new_L]);
        parent->values[i - 1] = std::move(V[new_L]);

        R->keys.assign(std::make_move_iterator(K.begin() + new_L + 1), std::make_move_iterator(K.end()));
        R->values.assign(std::make_move_iterator(V.begin() + new_L + 1),  std::make_move_iterator(V.end()));
        if(!L->is_leaf) {
            R->children.assign(C.begin() + new_L + 1, C.end());
        }
    }
    void redistribute_to_right(Node* parent, int i) {
        redist_count_++;
        Node* L = parent->children[i];
        Node* R = parent->children[i + 1];
        const int total = (int)L->keys.size() + 1 + (int)R->keys.size();
        const int new_L = total / 2;
        std::vector<Key> K; 
        K.reserve(total);
        std::vector<Value> V; 
        V.reserve(total);
        for(int j = 0; j < (int)L->keys.size(); j++) {
            K.push_back(std::move(L->keys[j]));
            V.push_back(std::move(L->values[j]));
        }
        K.push_back(std::move(parent->keys[i]));
        V.push_back(std::move(parent->values[i]));
        for(int j = 0; j < (int)R->keys.size(); j++) {
            K.push_back(std::move(R->keys[j]));
            V.push_back(std::move(R->values[j]));
        }
        std::vector<Node*> C;
        if(!L->is_leaf) {
            for(Node* c : L->children) {
                C.push_back(c);
            } 
            for(Node* c : R->children) {
                C.push_back(c);
            }
        }

        L->keys.assign(std::make_move_iterator(K.begin()), std::make_move_iterator(K.begin() + new_L));
        L->values.assign(std::make_move_iterator(V.begin()), std::make_move_iterator(V.begin() + new_L));
        if(!L->is_leaf) {
            L->children.assign(C.begin(), C.begin() + new_L + 1);
        }
        parent->keys[i] = std::move(K[new_L]);
        parent->values[i] = std::move(V[new_L]);
        R->keys.assign(std::make_move_iterator(K.begin() + new_L + 1), std::make_move_iterator(K.end()));
        R->values.assign(std::make_move_iterator(V.begin() + new_L + 1), std::make_move_iterator(V.end()));
        if(!L->is_leaf) {
            R->children.assign(C.begin() + new_L + 1, C.end());
        }
    }

    void split_two_into_three(Node* parent, int i) {
        split_count_++;
        Node* L = parent->children[i];
        Node* R = parent->children[i + 1];

        const int total = (int)L->keys.size() + 1 + (int)R->keys.size();
        std::vector<Key> K; 
        K.reserve(total);
        std::vector<Value> V; 
        V.reserve(total);
        for(int j = 0; j < (int)L->keys.size(); j++) {
            K.push_back(std::move(L->keys[j]));
            V.push_back(std::move(L->values[j]));
        }
        K.push_back(std::move(parent->keys[i]));
        V.push_back(std::move(parent->values[i]));
        for(int j = 0; j < (int)R->keys.size(); j++) {
            K.push_back(std::move(R->keys[j]));
            V.push_back(std::move(R->values[j]));
        }
        std::vector<Node*> C;
        if(!L->is_leaf) {
            for(Node* c : L->children) {
                C.push_back(c);
            }
            for(Node* c : R->children) {
                C.push_back(c);
            }
        }
        const int keys_to_dist = total - 2;
        const int base  = keys_to_dist / 3;
        const int extra = keys_to_dist % 3;
        const int a = base + (extra >= 1 ? 1 : 0);
        const int b = base + (extra >= 2 ? 1 : 0);
        Node* M_node = new Node(L->is_leaf);

        L->keys.assign(std::make_move_iterator(K.begin()), std::make_move_iterator(K.begin() + a));
        L->values.assign(std::make_move_iterator(V.begin()), std::make_move_iterator(V.begin() + a));
        if(!L->is_leaf) {
            L->children.assign(C.begin(), C.begin() + a + 1);
        }
        Key sep1_k = std::move(K[a]);
        Value sep1_v = std::move(V[a]);

        M_node->keys.assign(std::make_move_iterator(K.begin() + a + 1), std::make_move_iterator(K.begin() + a + 1 + b));
        M_node->values.assign(std::make_move_iterator(V.begin() + a + 1), std::make_move_iterator(V.begin() + a + 1 + b));
        if(!L->is_leaf) {
            M_node->children.assign(C.begin() + a + 1, C.begin() + a + 1 + b + 1);
        }
        Key sep2_k = std::move(K[a + 1 + b]);
        Value sep2_v = std::move(V[a + 1 + b]);
        R->keys.assign(std::make_move_iterator(K.begin() + a + b + 2), std::make_move_iterator(K.end()));
        R->values.assign(std::make_move_iterator(V.begin() + a + b + 2), std::make_move_iterator(V.end()));
        if(!L->is_leaf) {
            R->children.assign(C.begin() + a + b + 2, C.end());
        }

        parent->keys[i] = std::move(sep1_k);
        parent->values[i] = std::move(sep1_v);
        parent->keys.insert(  parent->keys.begin()   + i + 1, std::move(sep2_k));
        parent->values.insert(parent->values.begin() + i + 1, std::move(sep2_v));
        parent->children.insert(parent->children.begin() + i + 1, M_node);
    }
};
