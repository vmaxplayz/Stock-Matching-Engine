#include "price_tree.h"
static int height(const PriceLevel *node) {
    return node ? node->height : 0;
}

static void update(PriceLevel *n) {
    int l = height(n->left), r = height(n->right);
    n->height = 1 + (l > r ? l : r);
}

static PriceLevel *rotate_right(PriceLevel *y) {
    PriceLevel *x = y->left;

    y->left = x->right;
    x->right = y;
    update(y);
    update(x);
    return x;
}

static PriceLevel *rotate_left(PriceLevel *x) {
    PriceLevel *y = x->right;

    x->right = y->left;
    y->left = x;
    update(x);
    update(y);
    return y;
}

static PriceLevel *balance(PriceLevel *n) {
    int d;

    if (!n) return NULL;
    update(n); d = height(n->left) - height(n->right);
    if (d > 1) {
        if (height(n->left->left) < height(n->left->right)) n->left = rotate_left(n->left);
        return rotate_right(n);
    }
    if (d < -1) {
        if (height(n->right->right) < height(n->right->left)) n->right = rotate_right(n->right);
        return rotate_left(n);
    }
    return n;
}
PriceLevel *price_tree_find(PriceLevel *n, Price p) {
    while (n && n->price != p) n = p < n->price ? n->left : n->right;
    return n;
}
PriceLevel *price_tree_best(PriceLevel *n, Side side) {
    if (!n) return NULL;
    if (side == SIDE_BUY) while (n->right) n = n->right;
    else while (n->left) n = n->left;
    return n;
}
PriceLevel *price_tree_insert(PriceLevel *root, PriceLevel *node) {
    if (!root) return node;
    if (node->price < root->price) root->left = price_tree_insert(root->left, node);
    else root->right = price_tree_insert(root->right, node);
    return balance(root);
}
static PriceLevel *detach_min(PriceLevel *root, PriceLevel **minimum) {
    if (!root->left) { *minimum = root; return root->right; }
    root->left = detach_min(root->left, minimum);
    return balance(root);
}
PriceLevel *price_tree_remove(PriceLevel *root, Price p) {
    PriceLevel *successor, *right;
    if (!root) return NULL;
    if (p < root->price) root->left = price_tree_remove(root->left, p);
    else if (p > root->price) root->right = price_tree_remove(root->right, p);
    else {
        if (!root->left) return root->right;
        if (!root->right) return root->left;
        right = detach_min(root->right, &successor);
        successor->left = root->left; successor->right = right;
        return balance(successor);
    }
    return balance(root);
}
