#ifndef __ORDERSTATISTICSADAPTOR_HPP__
#define __ORDERSTATISTICSADAPTOR_HPP__

#include <algorithm>
#include <functional>
#include <iostream>

#include "utils.hpp"

using namespace Utils;
using namespace Message;

template <typename T, typename Compare = std::less<T>>
class OrderStatisticsTree {
 private:
  struct Node {
    T key;
    int size;  // Number of nodes in the subtree rooted at this node
    Node* left;
    Node* right;

    Node(const T& k) : key(k), size(1), left(nullptr), right(nullptr) {}
  };

  Node* root;

  // Helper function to get the size of a node (handles null nodes)
  int getSize(Node* node) { return (node != nullptr) ? node->size : 0; }

  // Helper function to update the size of a node based on its children
  void updateSize(Node* node) {
    if (node != nullptr) {
      node->size = 1 + getSize(node->left) + getSize(node->right);
    }
  }

  // Rotate right around the given node
  Node* rotateRight(Node* y) {
    Node* x = y->left;
    y->left = x->right;
    x->right = y;
    y->size = getSize(y->left) + getSize(y->right) + 1;
    x->size = getSize(x->left) + getSize(x->right) + 1;
    return x;
  }

  // Rotate left around the given node
  Node* rotateLeft(Node* x) {
    Node* y = x->right;
    x->right = y->left;
    y->left = x;
    x->size = getSize(x->left) + getSize(x->right) + 1;
    y->size = getSize(y->left) + getSize(y->right) + 1;
    return y;
  }

  // Insert a key into the tree rooted at the given node
  Node* insert(Node* node, const T& key) {
    if (node == nullptr) {
      return new Node(key);
    }

    if (Compare()(key, node->key)) {
      node->left = insert(node->left, key);
    } else {
      node->right = insert(node->right, key);
    }

    // Update the size of the current node
    updateSize(node);

    // Check balance and perform rotations if needed
    int balance = getSize(node->left) - getSize(node->right);

    // Left Heavy
    if (balance > 1) {
      // Left-Right Case
      if (Compare()(node->left->key, key)) {
        node->left = rotateLeft(node->left);
      }
      // Left-Left Case
      return rotateRight(node);
    }
    // Right Heavy
    if (balance < -1) {
      // Right-Left Case
      if (Compare()(key, node->right->key)) {
        node->right = rotateRight(node->right);
      }
      // Right-Right Case
      return rotateLeft(node);
    }

    return node;
  }

  // Get the kth smallest element in the tree rooted at the given node
  T kthSmallest(Node* node, int k) {
    if (node == nullptr || k <= 0 || k > node->size) {
      return T();  // Invalid k
    }

    int leftSize = getSize(node->left);

    if (k == leftSize + 1) {
      return node->key;  // Found the kth smallest element
    } else if (k <= leftSize) {
      return kthSmallest(node->left, k);
    } else {
      return kthSmallest(node->right, k - leftSize - 1);
    }
  }

  // Helper function to find the node with the minimum key in the tree
  Node* findMin(Node* node) {
    while (node->left != nullptr) {
      node = node->left;
    }
    return node;
  }

  // Helper function to delete a key from the tree rooted at the given node
  Node* deleteNode(Node* node, const T& key) {
    if (node == nullptr) {
      return nullptr;  // Key not found
    }

    if (Compare()(key, node->key)) {
      node->left = deleteNode(node->left, key);
    } else if (Compare()(node->key, key)) {
      node->right = deleteNode(node->right, key);
    } else {
      // Key found, perform deletion

      // Node with only one child or no child
      if (node->left == nullptr) {
        Node* temp = node->right;
        delete node;
        return temp;
      } else if (node->right == nullptr) {
        Node* temp = node->left;
        delete node;
        return temp;
      }

      // Node with two children, get the inorder successor (smallest in the right subtree)
      Node* temp = findMin(node->right);

      // Copy the inorder successor's key to this node
      node->key = temp->key;

      // Delete the inorder successor
      node->right = deleteNode(node->right, temp->key);
    }

    // Update the size of the current node
    updateSize(node);

    // Check balance and perform rotations if needed
    int balance = getSize(node->left) - getSize(node->right);

    // Left Heavy
    if (balance > 1) {
      // Left-Right Case
      if (getSize(node->left->right) > getSize(node->left->left)) {
        node->left = rotateLeft(node->left);
      }
      // Left-Left Case
      return rotateRight(node);
    }
    // Right Heavy
    if (balance < -1) {
      // Right-Left Case
      if (getSize(node->right->left) > getSize(node->right->right)) {
        node->right = rotateRight(node->right);
      }
      // Right-Right Case
      return rotateLeft(node);
    }

    return node;
  }

  // In order traversal
  void printInOrderHelper(Node* node) {
    if (node == nullptr)
      return;

    printInOrderHelper(node->left);
    std::cout << node->key << " ";
    printInOrderHelper(node->right);
  }

 public:
  OrderStatisticsTree() : root(nullptr) {}

  // Public method to insert a key into the tree
  void insert(const T& key) { root = insert(root, key); }

  // Public method to delete a key from the tree
  void deleteKey(const T& key) { root = deleteNode(root, key); }

  // Public method to get the kth smallest element
  T getKthSmallest(int k) { return kthSmallest(root, k); }

  // In order traversal
  void printInOrder() {
    printInOrderHelper(root);
    std::cout << std::endl;
  }
};

template <typename Compare, int N>
class OrderStatisticsTreeAdaptor {
 public:
  OrderStatisticsTreeAdaptor() : book_{OrderStatisticsTree<order, Compare>{}} {}

  void insert_(const order&);
  void remove_(const order&);
  void update_(const order&);
  void execute_(const order&);

 private:
  OrderStatisticsTree<order, Compare> book_;
};

#include "orderstatisticstreeadaptor_impl.hpp"

#endif
