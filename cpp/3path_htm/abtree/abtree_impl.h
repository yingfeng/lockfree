/* 
 * File:   abtree_impl.h
 * Author: trbot
 *
 * Created on September 27, 2015, 6:38 PM
 * 
 * Why is this code so long?
 * - Because this file defines THREE implementations
 *   (1) transactional lock elision (suffix _tle)
 *   (2) hybrid tm based implementation (suffix _tm) -- currently bugged
 *   (3) 3-path implementation (suffixes _fallback, _middle, _fast)
 * - Because the LLX and SCX synchronization primitives are implemented here
 *   (including memory reclamation for SCX records)
 */

#ifndef ABTREE_IMPL_H
#define	ABTREE_IMPL_H

#include <cassert>
#include <pthread.h>
#include "abtree.h"
#include "../globals_extern.h"
#include <rtm.h>
using namespace std;

#ifdef TM
#include <setjmp.h>
__thread sigjmp_buf ___jbuf;
#endif

//#define NO_REBALANCING

#define DO_VALIDATION VALIDATEOPS { this->debugPrint(); cout<<endl; if (!this->validate(0, false)) { exit(-1); } }

template<int DEGREE, typename K, class Compare, class RecManager>
__rtm_force_inline abtree_SCXRecord<DEGREE,K>* abtree<DEGREE,K,Compare,RecManager>::allocateSCXRecord(
            const int tid) {
    abtree_SCXRecord<DEGREE,K> *newop = recordmgr->template allocate<abtree_SCXRecord<DEGREE,K> >(tid);
    if (newop == NULL) {
        COUTATOMICTID("ERROR: could not allocate scx record"<<endl);
        exit(-1);
    }
    return newop;
}

template<int DEGREE, typename K, class Compare, class RecManager>
__rtm_force_inline abtree_Node<DEGREE,K>* abtree<DEGREE,K,Compare,RecManager>::allocateNode(
            const int tid) {
    abtree_Node<DEGREE,K> *newnode = recordmgr->template allocate<abtree_Node<DEGREE,K> >(tid);
    if (newnode == NULL) {
        COUTATOMICTID("ERROR: could not allocate node"<<endl);
        exit(-1);
    }
    return newnode;
}

template<int DEGREE, typename K, class Compare, class RecManager>
abtree_SCXRecord<DEGREE,K> * abtree<DEGREE,K,Compare,RecManager>::createSCXRecord(const int tid, void * volatile * const field, abtree_Node<DEGREE,K> * const newNode, abtree_Node<DEGREE,K> ** const nodes, abtree_SCXRecord<DEGREE,K> ** const scxRecordsSeen, const int numberOfNodes, const int numberOfNodesToFreeze) {
    abtree_SCXRecord<DEGREE,K> * result = GET_ALLOCATED_SCXRECORD_PTR(tid);
    result->allFrozen = false;
    result->field = field;
    result->newNode = newNode;
    result->state = abtree_SCXRecord<DEGREE,K>::STATE_INPROGRESS;
    for (int i=0;i<numberOfNodes;++i) {
        result->nodes[i] = nodes[i];
    }
    for (int i=0;i<numberOfNodes;++i) {
        result->scxRecordsSeen[i] = scxRecordsSeen[i];
    }
    result->numberOfNodes = numberOfNodes;
    result->numberOfNodesToFreeze = numberOfNodesToFreeze;
    return result;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::isSentinel(abtree_Node<DEGREE,K> * node) {
    return (node == root || node == root->ptrs[0]);
}

template<int DEGREE, typename K, class Compare, class RecManager>
long long abtree<DEGREE,K,Compare,RecManager>::debugKeySum(abtree_Node<DEGREE,K> * node) {
    if (node == NULL) return 0;
    long long sum = 0;
    if (node->isLeaf()) {
        for (int i=0;i<node->getABDegree();++i) {
            sum += node->keys[i];
        }
    } else {
        for (int i=0;i<node->getABDegree();++i) {
            sum += debugKeySum((abtree_Node<DEGREE,K> *) node->ptrs[i]);
        }
    }
    return sum;
}









template<int DEGREE, typename K, class Compare, class RecManager>
const pair<void*,bool> abtree<DEGREE,K,Compare,RecManager>::find_tle(const int tid, const K& key) {
    pair<void*,bool> result;
    this->recordmgr->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, this->counters->pathSuccess, this->counters->pathFail, this->counters->htmAbort);
    
    abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
    while (!l->isLeaf()) {
        l = l->getChild(key, cmp);
    }
    int index = l->getKeyIndex(key, cmp);
    if (index < l->getKeyCount()) {
        result.first = l->ptrs[index];
        result.second = true;
    } else {
        result.first = NO_VALUE;
        result.second = false;
    }

    scope.end();
    this->recordmgr->enterQuiescentState(tid);
    return result;
}

template<int DEGREE, typename K, class Compare, class RecManager>
int abtree<DEGREE,K,Compare,RecManager>::rangeQuery_tle(const int tid, const K& low, const K& hi, abtree_Node<DEGREE,K> const ** result) {
    int cnt = 0;
    block<abtree_Node<DEGREE,K> > stack (NULL);

    this->recordmgr->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, this->counters->pathSuccess, this->counters->pathFail, this->counters->htmAbort);

    // depth first traversal (of interesting subtrees)
    stack.push(root);
    while (!stack.isEmpty()) {
        abtree_Node<DEGREE,K> * node = stack.pop();

        // if internal node, explore its children
        if (!node->isLeaf()) {
            // find right-most sub-tree that could contain a key in [lo, hi]
            int nkeys = node->getKeyCount();
            int r = nkeys;
            while (r > 0 && cmp(hi, (const K&) node->keys[r-1])) { // subtree rooted at u.c.get(r) contains only keys > hi
                --r;
            }
            // find left-most sub-tree that could contain a key in [lo, hi]
            int l = 0;
            while (l < nkeys && !cmp(low, (const K&) node->keys[l])) {
                ++l;
            }
            // perform DFS from left to right (so push onto stack from right to left)
            for (int i=r;i>=l; --i) {
                stack.push((abtree_Node<DEGREE,K> *) node->ptrs[i]);
            }

        // else if leaf node, add it to the result that will be returned
        } else {
            result[cnt++] = node;
        }
    }
    scope.end();
    this->recordmgr->enterQuiescentState(tid);
    return cnt;
}

template<int DEGREE, typename K, class Compare, class RecManager>
const void * abtree<DEGREE,K,Compare,RecManager>::insert_tle(
            const int tid, const K& key, void * const val) {
    void * result;
    
    abtree_Node<DEGREE,K> * p;
    abtree_Node<DEGREE,K> * l;
    int keyindexl;
    int lindex;
    int nkeysl;
    bool found;
    abtree_Node<DEGREE,K> * parent = GET_ALLOCATED_NODE_PTR(tid, 0);
    abtree_Node<DEGREE,K> * left = GET_ALLOCATED_NODE_PTR(tid, 1);
    abtree_Node<DEGREE,K> * right;
    kvpair<K> tosort[DEGREE+1];

    if (parent->marked) parent->marked = false;
    parent->scxRecord = dummy;
    if (left->marked) left->marked = false;
    left->scxRecord = dummy;

    this->recordmgr->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, this->counters->pathSuccess, this->counters->pathFail, this->counters->htmAbort);

    p = root;
    l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
    while (!l->isLeaf()) {
        p = l;
        l = l->getChild(key, cmp);
    }
    keyindexl = l->getKeyIndex(key, cmp);
    lindex = p->getChildIndex(key, cmp);
    nkeysl = l->getKeyCount();

    found = (keyindexl < nkeysl);
    if (found) {
        result = l->ptrs[keyindexl];
        l->ptrs[keyindexl] = val;
        scope.end();
        this->recordmgr->enterQuiescentState(tid);
    } else {
        if (nkeysl < DEGREE) {
            // inserting new key/value pair into leaf
            l->keys[nkeysl] = key;
            l->ptrs[nkeysl] = val;
            ++l->size;
            scope.end();
            this->recordmgr->enterQuiescentState(tid);
            result = NO_VALUE;
        } else { // nkeysl == DEGREE
            // overflow: insert a new tagged parent above l and create a new sibling
            right = l;

            for (int i=0;i<nkeysl;++i) {
                tosort[i].key = l->keys[i];
                tosort[i].val = l->ptrs[i];
            }
            tosort[nkeysl].key = key;
            tosort[nkeysl].val = val;
            qsort(tosort, nkeysl+1, sizeof(kvpair<K>), kv_compare<K,Compare>);

            const int leftLength = (nkeysl+1)/2;
            for (int i=0;i<leftLength;++i) {
                left->keys[i] = tosort[i].key;
            }
            for (int i=0;i<leftLength;++i) {
                left->ptrs[i] = tosort[i].val;
            }
            left->tag = false;
            left->size = leftLength;
            left->leaf = true;

            const int rightLength = (nkeysl+1) - leftLength;
            for (int i=0;i<rightLength;++i) {
                right->keys[i] = tosort[i+leftLength].key;
            }
            for (int i=0;i<rightLength;++i) {
                right->ptrs[i] = tosort[i+leftLength].val;
            }
            right->size = rightLength;

            parent->keys[0] = right->keys[0];
            parent->ptrs[0] = left;
            parent->ptrs[1] = right;
            parent->tag = (p != root);
            parent->size = 2;
            parent->leaf = false;

            p->ptrs[lindex] = parent;
            scope.end();
            this->recordmgr->enterQuiescentState(tid);

            result = NO_VALUE;

            // do memory reclamation and allocation
            REPLACE_ALLOCATED_NODE(tid, 0);
            REPLACE_ALLOCATED_NODE(tid, 1);
            
            // do rebalancing
            while (!rebalance_tle(tid, key)) {}
        }
    }
    return result;
}

template<int DEGREE, typename K, class Compare, class RecManager>
const pair<void *,bool> abtree<DEGREE,K,Compare,RecManager>::erase_tle(
            const int tid, const K& key) { // input consists of: const K& key
    pair<void *,bool> result;
    abtree_Node<DEGREE,K> * p;
    abtree_Node<DEGREE,K> * l;
    int keyindexl;
    int nkeysl;
    bool found;
    
    this->recordmgr->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, this->counters->pathSuccess, this->counters->pathFail, this->counters->htmAbort);

    p = root;
    l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
    while (!l->isLeaf()) {
        p = l;
        l = l->getChild(key, cmp);
    }

    keyindexl = l->getKeyIndex(key, cmp);
    nkeysl = l->getKeyCount();

    found = (keyindexl < nkeysl);
    if (!found) {
        scope.end();
        this->recordmgr->enterQuiescentState(tid);
        
        result.first = NO_VALUE;
        result.second = false;
    } else {
        // delete key/value pair from leaf
        result.first = l->ptrs[keyindexl];
        l->keys[keyindexl] = l->keys[nkeysl-1];
        l->ptrs[keyindexl] = l->ptrs[nkeysl-1];
        --l->size;
        scope.end();
        this->recordmgr->enterQuiescentState(tid);
        
        // do rebalancing
        if (nkeysl < MIN_DEGREE) {
            while (!rebalance_tle(tid, key)) {}
        }
        result.second = true;
    }
    return result;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::rebalance_tle(const int tid, const K& key) {
    // scx record fields to populate:
    // numberOfNodesAllocated, numberOfNodesToFreeze, numberOfNodes, newNode, nodes, scxRecordsSeen, field

//    abtree_Node<DEGREE,K> * p0 = GET_ALLOCATED_NODE_PTR(tid, 0);
//    abtree_Node<DEGREE,K> * p1 = GET_ALLOCATED_NODE_PTR(tid, 1);
//    abtree_Node<DEGREE,K> * p2 = GET_ALLOCATED_NODE_PTR(tid, 2);
//    p0->marked = false;
//    p1->marked = false;
//    p2->marked = false;

    this->recordmgr->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, this->counters->pathSuccess, this->counters->pathFail, this->counters->htmAbort);
    
    abtree_Node<DEGREE,K> * gp = root;
    abtree_Node<DEGREE,K> * p = root;
    abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) root->ptrs[0];

    // Unrolled special case for root:
    // Note: l is NOT tagged, but it might have only 1 child pointer, which would be a problem
    if (l->isLeaf()) {
        scope.end();
        this->recordmgr->enterQuiescentState(tid);
        return true; // nothing can be wrong with the root, if it is a leaf
    }
    if (l->getABDegree() == 1) { // root is internal and has only one child
        rootJoinParent_tle(tid, p, l, p->getChildIndex(key, cmp), &scope);
        return false;
    }

    // root is internal, and there is nothing wrong with it, so move on
    gp = p;
    p = l;
    l = l->getChild(key, cmp);

    // check each subsequent node for tag violations and degree violations
    while (!(l->isLeaf() || l->tag || (l->getABDegree() < MIN_DEGREE && !isSentinel(l)))) {
        gp = p;
        p = l;
        l = l->getChild(key, cmp);
    }

    if (!(l->tag || (l->getABDegree() < MIN_DEGREE && !isSentinel(l)))) {
        scope.end();
        this->recordmgr->enterQuiescentState(tid);
        return true; // no violations to fix
    }

    // tag operations take precedence
    if (l->tag) {
        if (p->getABDegree() + l->getABDegree() <= DEGREE+1) {
            tagJoinParent_tle(tid, gp, p, gp->getChildIndex(key, cmp), l, p->getChildIndex(key, cmp), &scope);
        } else {
            tagSplit_tle(tid, gp, p, gp->getChildIndex(key, cmp), l, p->getChildIndex(key, cmp), &scope);
        }
    } else { // assert (l->getABDegree() < MIN_DEGREE)
        // get sibling of l
        abtree_Node<DEGREE,K> * s;
        int lindex = p->getChildIndex(key, cmp);
        //if (p->ptrs[lindex] != l) return false;
        int sindex = lindex ? lindex-1 : lindex+1;
        s = (abtree_Node<DEGREE,K> *) p->ptrs[sindex];

        // tag operations take precedence
        if (s->tag) {
            if (p->getABDegree() + s->getABDegree() <= DEGREE+1) {
                tagJoinParent_tle(tid, gp, p, gp->getChildIndex(key, cmp), s, sindex, &scope);
            } else {
                tagSplit_tle(tid, gp, p, gp->getChildIndex(key, cmp), s, sindex, &scope);
            }
        } else {
            // either join l and s, or redistribute keys between them
            if (l->getABDegree() + s->getABDegree() < 2*MIN_DEGREE) {
                joinSibling_tle(tid, gp, p, gp->getChildIndex(key, cmp), l, lindex, s, sindex, &scope);
            } else {
                redistributeSibling_tle(tid, gp, p, gp->getChildIndex(key, cmp), l, lindex, s, sindex, &scope);
            }
        }
    }
    return false;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::rootJoinParent_tle(const int tid, abtree_Node<DEGREE,K> * const p, abtree_Node<DEGREE,K> * const l, const int lindex, TLEScope * const scope) {
    abtree_Node<DEGREE,K> * c = (abtree_Node<DEGREE,K> *) l->ptrs[0];
    abtree_Node<DEGREE,K> * newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
    newNode->marked = false;
    newNode->scxRecord = dummy;

    for (int i=0;i<c->getKeyCount();++i) {
        newNode->keys[i] = c->keys[i];
    }
    for (int i=0;i<c->getABDegree();++i) {
        newNode->ptrs[i] = c->ptrs[i];
    }

    newNode->tag = false; // since p is root(holder), newNode is the new actual root, so its tag is false
    newNode->size = c->size;
    newNode->leaf = c->leaf;

    p->ptrs[lindex] = newNode;
    scope->end();
    this->recordmgr->enterQuiescentState(tid);
    
    REPLACE_ALLOCATED_NODE(tid, 0);
    recordmgr->retire(tid, c);
    recordmgr->retire(tid, l);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::tagJoinParent_tle(const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, TLEScope * const scope) {
    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->marked = false;
    newp->scxRecord = dummy;

    // elements of p left of l
    int k1=0, k2=0;
    for (int i=0;i<lindex;++i) {
        newp->keys[k1++] = p->keys[i];
    }
    for (int i=0;i<lindex;++i) {
        newp->ptrs[k2++] = p->ptrs[i];
    }

    // contents of l
    for (int i=0;i<l->getKeyCount();++i) {
        newp->keys[k1++] = l->keys[i];
    }
    for (int i=0;i<l->getABDegree();++i) {
        newp->ptrs[k2++] = l->ptrs[i];
    }

    // remaining elements of p
    for (int i=lindex;i<p->getKeyCount();++i) {
        newp->keys[k1++] = p->keys[i];
    }
    // skip child pointer for lindex
    for (int i=lindex+1;i<p->getABDegree();++i) {
        newp->ptrs[k2++] = p->ptrs[i];
    }
    
    newp->tag = false;
    newp->size = p->size + l->size - 1;
    newp->leaf = false;

    gp->ptrs[pindex] = newp;
    scope->end();
    this->recordmgr->enterQuiescentState(tid);
    
    REPLACE_ALLOCATED_NODE(tid, 0);
    recordmgr->retire(tid, l);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::tagSplit_tle(const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, TLEScope * const scope) {
    // create new nodes for update
    const int sz = p->getABDegree() + l->getABDegree() - 1;
    const int leftsz = sz/2;
    const int rightsz = sz - leftsz;
    
    K keys[2*DEGREE+1];
    void * ptrs[2*DEGREE+1];
    int k1=0, k2=0;

    // elements of p left than l
    for (int i=0;i<lindex;++i) {
        keys[k1++] = p->keys[i];
    }
    for (int i=0;i<lindex;++i) {
        ptrs[k2++] = p->ptrs[i];
    }

    // contents of l
    for (int i=0;i<l->getKeyCount();++i) {
        keys[k1++] = l->keys[i];
    }
    for (int i=0;i<l->getABDegree();++i) {
        ptrs[k2++] = l->ptrs[i];
    }

    // remaining elements of p
    for (int i=lindex;i<p->getKeyCount();++i) {
        keys[k1++] = p->keys[i];
    }
    // skip child pointer for lindex
    for (int i=lindex+1;i<p->getABDegree();++i) {
        ptrs[k2++] = p->ptrs[i];
    }
    
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newleft = GET_ALLOCATED_NODE_PTR(tid, 1);
    newleft->scxRecord = dummy;
    newleft->marked = false;

    abtree_Node<DEGREE,K> * newright = GET_ALLOCATED_NODE_PTR(tid, 2);
    newright->scxRecord = dummy;
    newright->marked = false;

    k1=0;
    k2=0;
    
    for (int i=0;i<leftsz-1;++i) {
        newleft->keys[i] = keys[k1++];
    }
    for (int i=0;i<leftsz;++i) {
        newleft->ptrs[i] = ptrs[k2++];
    }
    newleft->tag = false;
    newleft->size = leftsz;
    newleft->leaf = false;
    
    newp->keys[0] = keys[k1++];
    newp->ptrs[0] = newleft;
    newp->ptrs[1] = newright;
    newp->tag = (gp != root);
    newp->size = 2;
    newp->leaf = false;
    
    for (int i=0;i<rightsz-1;++i) {
        newright->keys[i] = keys[k1++];
    }
    for (int i=0;i<rightsz;++i) {
        newright->ptrs[i] = ptrs[k2++];
    }
    newright->tag = false;
    newright->size = rightsz;
    newright->leaf = false;
    
    gp->ptrs[pindex] = newp;
    scope->end();
    this->recordmgr->enterQuiescentState(tid);
    
    recordmgr->retire(tid, l);
    recordmgr->retire(tid, p);
    REPLACE_ALLOCATED_NODE(tid, 0);
    REPLACE_ALLOCATED_NODE(tid, 1);
    REPLACE_ALLOCATED_NODE(tid, 2);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::joinSibling_tle(const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex, TLEScope * const scope) {
    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newl = GET_ALLOCATED_NODE_PTR(tid, 1);
    newl->scxRecord = dummy;
    newl->marked = false;

    // create newl by joining s to l

    abtree_Node<DEGREE,K> * left;
    abtree_Node<DEGREE,K> * right;
    int leftindex;
    int rightindex;
    if (lindex < sindex) {
        left = l;
        leftindex = lindex;
        right = s;
        rightindex = sindex;
    } else {
        left = s;
        leftindex = sindex;
        right = l;
        rightindex = lindex;
    }
    
    int k1=0, k2=0;
    for (int i=0;i<left->getKeyCount();++i) {
        newl->keys[k1++] = left->keys[i];
    }
    for (int i=0;i<left->getABDegree();++i) {
        newl->ptrs[k2++] = left->ptrs[i];
    }
    if (!left->isLeaf()) newl->keys[k1++] = p->keys[leftindex];
    for (int i=0;i<right->getKeyCount();++i) {
        newl->keys[k1++] = right->keys[i];
    }
    for (int i=0;i<right->getABDegree();++i) {
        newl->ptrs[k2++] = right->ptrs[i];
    }
    
    // create newp from p by:
    // 1. skipping the key for leftindex and child pointer for sindex
    // 2. replacing l with newl
    for (int i=0;i<leftindex;++i) {
        newp->keys[i] = p->keys[i];
    }
    for (int i=0;i<sindex;++i) {
        newp->ptrs[i] = p->ptrs[i];
    }
    for (int i=leftindex+1;i<p->getKeyCount();++i) {
        newp->keys[i-1] = p->keys[i];
    }
    for (int i=sindex+1;i<p->getABDegree();++i) {
        newp->ptrs[i-1] = p->ptrs[i];
    }
    // replace l with newl
    newp->ptrs[lindex - (lindex > sindex)] = newl;
    
    newp->tag = false;
    newp->size = p->size - 1;
    newp->leaf = false;
    newl->tag = false;
    newl->size = l->size + s->size;
    newl->leaf = l->leaf;
    
    gp->ptrs[pindex] = newp;    
    scope->end();
    this->recordmgr->enterQuiescentState(tid);
    
    REPLACE_ALLOCATED_NODE(tid, 0);
    REPLACE_ALLOCATED_NODE(tid, 1);
    recordmgr->retire(tid, p);
    recordmgr->retire(tid, l);
    recordmgr->retire(tid, s);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::redistributeSibling_tle(const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex, TLEScope * const scope) {
    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newl = GET_ALLOCATED_NODE_PTR(tid, 1);
    newl->scxRecord = dummy;
    newl->marked = false;

    abtree_Node<DEGREE,K> * news = GET_ALLOCATED_NODE_PTR(tid, 2);
    news->scxRecord = dummy;
    news->marked = false;

    // create newl and news by evenly sharing the keys + pointers of l and s
    int sz = l->getABDegree() + s->getABDegree();
    int leftsz = sz/2;
    int rightsz = sz-leftsz;
    kvpair<K> tosort[2*DEGREE+1];
    
    abtree_Node<DEGREE,K> * left;
    abtree_Node<DEGREE,K> * right;
    abtree_Node<DEGREE,K> * newleft;
    abtree_Node<DEGREE,K> * newright;
    int leftindex;
    int rightindex;
    if (lindex < sindex) {
        left = l;
        newleft = newl;
        leftindex = lindex;
        right = s;
        newright = news;
        rightindex = sindex;
    } else {
        left = s;
        newleft = news;
        leftindex = sindex;
        right = l;
        newright = newl;
        rightindex = lindex;
    }
    assert(rightindex == 1+leftindex);
    
    // combine the contents of l and s (and one key from p)
    int k1=0, k2=0;
    for (int i=0;i<left->getKeyCount();++i) {
        tosort[k1++].key = left->keys[i];
    }
    for (int i=0;i<left->getABDegree();++i) {
        tosort[k2++].val = left->ptrs[i];
    }
    if (!left->isLeaf()) tosort[k1++].key = p->keys[leftindex];
    for (int i=0;i<right->getKeyCount();++i) {
        tosort[k1++].key = right->keys[i];
    }
    for (int i=0;i<right->getABDegree();++i) {
        tosort[k2++].val = right->ptrs[i];
    }
    //assert(k1 == sz+left->isLeaf()); // only holds in general if something like opacity is satisfied
    assert(!gp->tag);
    assert(!p->tag);
    assert(!left->tag);
    assert(!right->tag);
    assert(k1 <= sz+1);
    assert(k2 == sz);
    assert(!left->isLeaf() || k1 == k2);
    
    // sort if this is a leaf
    if (left->isLeaf()) qsort(tosort, k1, sizeof(kvpair<K>), kv_compare<K,Compare>);
    
    // distribute contents between newleft and newright
    k1=0;
    k2=0;
    for (int i=0;i<leftsz - !left->isLeaf();++i) {
        newleft->keys[i] = tosort[k1++].key;
    }
    for (int i=0;i<leftsz;++i) {
        newleft->ptrs[i] = tosort[k2++].val;
    }
    // reserve one key for the parent (to go between newleft and newright))
    K keyp = tosort[k1].key;
    if (!left->isLeaf()) ++k1;
    for (int i=0;i<rightsz - !left->isLeaf();++i) {
        newright->keys[i] = tosort[k1++].key;
    }
    for (int i=0;i<rightsz;++i) {
        newright->ptrs[i] = tosort[k2++].val;
    }
    
    // create newp from p by replacing left with newleft and right with newright,
    // and replacing one key (between these two pointers)
    for (int i=0;i<p->getKeyCount();++i) {
        newp->keys[i] = p->keys[i];
    }
    for (int i=0;i<p->getABDegree();++i) {
        newp->ptrs[i] = p->ptrs[i];
    }
    newp->keys[leftindex] = keyp;
    newp->ptrs[leftindex] = newleft;
    newp->ptrs[rightindex] = newright;
    newp->tag = false;
    newp->size = p->size;
    newp->leaf = false;
    
    newleft->tag = false;
    newleft->size = leftsz;
    newleft->leaf = left->leaf;
    newright->tag = false;
    newright->size = rightsz;
    newright->leaf = right->leaf;

    gp->ptrs[pindex] = newp;
    scope->end();
    this->recordmgr->enterQuiescentState(tid);

    REPLACE_ALLOCATED_NODE(tid, 0);
    REPLACE_ALLOCATED_NODE(tid, 1);
    REPLACE_ALLOCATED_NODE(tid, 2);
    recordmgr->retire(tid, l);
    recordmgr->retire(tid, s);
    recordmgr->retire(tid, p);
    return true;
}






/**
 * WARNING: THE HYBRID TM-BASED IMPLEMENTATION OF THE (A,B)-TREE HAS A BUG.
 * YOU CAN SEE IT WITH A 100% INSERT, 1M KEYRANGE WORKLOAD USING HYBRID NOREC.
 */

#ifdef TM
template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::isSentinel_tm(TM_ARGDECL_ALONE, abtree_Node<DEGREE,K> * node) {
    abtree_Node<DEGREE,K> * r = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(root);
    return (node == r || node == (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(r->ptrs[0]));
}

template<int DEGREE, typename K, class Compare, class RecManager>
const pair<void*,bool> abtree<DEGREE,K,Compare,RecManager>::find_tm(TM_ARGDECL_ALONE, const int tid, const K& key) {
    pair<void*,bool> result;
    this->recordmgr->leaveQuiescentState(tid);
    TM_BEGIN_RO(___jbuf);
    
    abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(root->ptrs[0]);
    while (!l->isLeaf_tm(TM_ARG_ALONE)) {
        l = l->getChild_tm(TM_ARG_ALONE, key, cmp);
    }
    int index = l->getKeyIndex_tm(TM_ARG_ALONE, key, cmp);
    if (index < l->getKeyCount_tm(TM_ARG_ALONE)) {
        result.first = TM_SHARED_READ_P(l->ptrs[index]);
        result.second = true;
    } else {
        result.first = NO_VALUE;
        result.second = false;
    }

    TM_END();
    this->recordmgr->enterQuiescentState(tid);
    return result;
}

template<int DEGREE, typename K, class Compare, class RecManager>
int abtree<DEGREE,K,Compare,RecManager>::rangeQuery_tm(TM_ARGDECL_ALONE, const int tid, const K& low, const K& hi, abtree_Node<DEGREE,K> const ** result) {
    int cnt;

    this->recordmgr->leaveQuiescentState(tid);
    TM_BEGIN_RO(___jbuf);
    block<abtree_Node<DEGREE,K> > stack (NULL);
    cnt = 0;

    // depth first traversal (of interesting subtrees)
    stack.push(root);
    while (!stack.isEmpty()) {
        abtree_Node<DEGREE,K> * node = stack.pop();

        // if internal node, explore its children
        if (!node->isLeaf_tm(TM_ARG_ALONE)) {
            // find right-most sub-tree that could contain a key in [lo, hi]
            int nkeys = node->getKeyCount_tm(TM_ARG_ALONE);
            int r = nkeys;
            while (r > 0 && cmp(hi, (const K&) TM_SHARED_READ_L(node->keys[r-1]))) { // subtree rooted at u.c.get(r) contains only keys > hi
                --r;
            }
            // find left-most sub-tree that could contain a key in [lo, hi]
            int l = 0;
            while (l < nkeys && !cmp(low, (const K&) TM_SHARED_READ_L(node->keys[l]))) {
                ++l;
            }
            // perform DFS from left to right (so push onto stack from right to left)
            for (int i=r;i>=l; --i) {
                stack.push((abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(node->ptrs[i]));
            }

        // else if leaf node, add it to the result that will be returned
        } else {
            result[cnt++] = node;
        }
    }
    TM_END();
    this->recordmgr->enterQuiescentState(tid);
    return cnt;
}

template<int DEGREE, typename K, class Compare, class RecManager>
const void * abtree<DEGREE,K,Compare,RecManager>::insert_tm(
            TM_ARGDECL_ALONE, const int tid, const K& key, void * const val) {
    void * result;
    
    this->recordmgr->leaveQuiescentState(tid);
    TM_BEGIN(___jbuf);

    abtree_Node<DEGREE,K> * p;
    abtree_Node<DEGREE,K> * l;
    long keyindexl;
    long lindex;
    long nkeysl;
    long found;
    abtree_Node<DEGREE,K> * parent = GET_ALLOCATED_NODE_PTR(tid, 0);
    abtree_Node<DEGREE,K> * left = GET_ALLOCATED_NODE_PTR(tid, 1);
    abtree_Node<DEGREE,K> * right;
    kvpair<K> tosort[DEGREE+1];

    if (parent->marked) parent->marked = false;
    parent->scxRecord = dummy;
    if (left->marked) left->marked = false;
    left->scxRecord = dummy;

    p = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(root);
    l = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(p->ptrs[0]);
    while (!l->isLeaf_tm(TM_ARG_ALONE)) {
        p = l;
        l = l->getChild_tm(TM_ARG_ALONE, key, cmp);
    }
    keyindexl = l->getKeyIndex_tm(TM_ARG_ALONE, key, cmp);
    lindex = p->getChildIndex_tm(TM_ARG_ALONE, key, cmp);
    nkeysl = l->getKeyCount_tm(TM_ARG_ALONE);

    found = (keyindexl < nkeysl);
    if (found) {
        result = (void *) TM_SHARED_READ_P(l->ptrs[keyindexl]);
        TM_SHARED_WRITE_P(l->ptrs[keyindexl], val);
        TM_END();
        this->recordmgr->enterQuiescentState(tid);
    } else {
        if (nkeysl < DEGREE) {
            // inserting new key/value pair into leaf
            TM_SHARED_WRITE_L(l->keys[nkeysl], key);
            TM_SHARED_WRITE_P(l->ptrs[nkeysl], val);
            TM_SHARED_WRITE_L(l->size, TM_SHARED_READ_L(l->size)+1);
            TM_END();
            this->recordmgr->enterQuiescentState(tid);
            result = NO_VALUE;
        } else { // nkeysl == DEGREE
            // overflow: insert a new tagged parent above l and create a new sibling
            right = l;

            for (long i=0;i<nkeysl;++i) {
                tosort[i].key = TM_SHARED_READ_L(l->keys[i]);
                tosort[i].val = (void *) TM_SHARED_READ_P(l->ptrs[i]);
            }
            tosort[nkeysl].key = key;
            tosort[nkeysl].val = val;
            qsort(tosort, nkeysl+1, sizeof(kvpair<K>), kv_compare<K,Compare>);

            const long leftLength = (nkeysl+1)/2;
            for (long i=0;i<leftLength;++i) {
                left->keys[i] = tosort[i].key;
            }
            for (long i=0;i<leftLength;++i) {
                left->ptrs[i] = tosort[i].val;
            }
            left->tag = false;
            left->size = leftLength;
            left->leaf = true;

            const long rightLength = (nkeysl+1) - leftLength;
            for (long i=0;i<rightLength;++i) {
                TM_SHARED_WRITE_L(right->keys[i], tosort[i+leftLength].key);
            }
            for (long i=0;i<rightLength;++i) {
                TM_SHARED_WRITE_P(right->ptrs[i], tosort[i+leftLength].val);
            }
            TM_SHARED_WRITE_L(right->size, rightLength);

            parent->keys[0] = TM_SHARED_READ_L(right->keys[0]);
            parent->ptrs[0] = left;
            parent->ptrs[1] = right;
            parent->tag = (p != (void *) TM_SHARED_READ_P(root));
            parent->size = 2;
            parent->leaf = false;

            TM_SHARED_WRITE_P(p->ptrs[lindex], parent);
            TM_END();
            this->recordmgr->enterQuiescentState(tid);

            result = NO_VALUE;

            // do memory reclamation and allocation
            REPLACE_ALLOCATED_NODE(tid, 0);
            REPLACE_ALLOCATED_NODE(tid, 1);
            
            // do rebalancing
            while (!rebalance_tm(TM_ARG_ALONE, tid, key)) {}
        }
    }
    return result;
}

template<int DEGREE, typename K, class Compare, class RecManager>
const pair<void *,bool> abtree<DEGREE,K,Compare,RecManager>::erase_tm(
            TM_ARGDECL_ALONE, const int tid, const K& key) { // input consists of: const K& key
    pair<void *,bool> result;
    abtree_Node<DEGREE,K> * p;
    abtree_Node<DEGREE,K> * l;
    int keyindexl;
    int nkeysl;
    bool found;
    
    this->recordmgr->leaveQuiescentState(tid);
    TM_BEGIN(___jbuf);

    p = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(root);
    l = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(root->ptrs[0]);
    while (!l->isLeaf_tm(TM_ARG_ALONE)) {
        p = l;
        l = l->getChild_tm(TM_ARG_ALONE, key, cmp);
    }

    keyindexl = l->getKeyIndex_tm(TM_ARG_ALONE, key, cmp);
    nkeysl = l->getKeyCount_tm(TM_ARG_ALONE);

    found = (keyindexl < nkeysl);
    if (!found) {
        TM_END();
        this->recordmgr->enterQuiescentState(tid);
        
        result.first = NO_VALUE;
        result.second = false;
    } else {
        // delete key/value pair from leaf
        result.first = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(l->ptrs[keyindexl]);
        TM_SHARED_WRITE_L(l->keys[keyindexl], TM_SHARED_READ_L(l->keys[nkeysl-1]));
        TM_SHARED_WRITE_P(l->ptrs[keyindexl], (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(l->ptrs[nkeysl-1]));
        TM_SHARED_WRITE_L(l->size, TM_SHARED_READ_L(l->size)-1);
        TM_END();
        this->recordmgr->enterQuiescentState(tid);
        
        // do rebalancing
        if (nkeysl < MIN_DEGREE) {
            while (!rebalance_tm(TM_ARG_ALONE, tid, key)) {}
        }
        result.second = true;
    }
    return result;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::rebalance_tm(TM_ARGDECL_ALONE, const int tid, const K& key) {
    // scx record fields to populate:
    // numberOfNodesAllocated, numberOfNodesToFreeze, numberOfNodes, newNode, nodes, scxRecordsSeen, field

//    abtree_Node<DEGREE,K> * p0 = GET_ALLOCATED_NODE_PTR(tid, 0);
//    abtree_Node<DEGREE,K> * p1 = GET_ALLOCATED_NODE_PTR(tid, 1);
//    abtree_Node<DEGREE,K> * p2 = GET_ALLOCATED_NODE_PTR(tid, 2);
//    p0->marked = false;
//    p1->marked = false;
//    p2->marked = false;
return true; // TODO: REMOVE THIS

    this->recordmgr->leaveQuiescentState(tid);
    TM_BEGIN(___jbuf);
    
    abtree_Node<DEGREE,K> * gp = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(root);
    abtree_Node<DEGREE,K> * p = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(root);
    abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(root->ptrs[0]);
    // Unrolled special case for root:
    // Note: l is NOT tagged, but it might have only 1 child pointer, which would be a problem
    if (l->isLeaf_tm(TM_ARG_ALONE)) {
        TM_END();
        this->recordmgr->enterQuiescentState(tid);
        return true; // nothing can be wrong with the root, if it is a leaf
    }
    if (l->getABDegree_tm(TM_ARG_ALONE) == 1) { // root is internal and has only one child
        rootJoinParent_tm(TM_ARG_ALONE, tid, p, l, p->getChildIndex_tm(TM_ARG_ALONE, key, cmp));
        return false;
    }
    // root is internal, and there is nothing wrong with it, so move on
    gp = p;
    p = l;
    l = l->getChild_tm(TM_ARG_ALONE, key, cmp);

    // check each subsequent node for tag violations and degree violations
    while (!(l->isLeaf_tm(TM_ARG_ALONE) || TM_SHARED_READ_L(l->tag) || (l->getABDegree_tm(TM_ARG_ALONE) < MIN_DEGREE && !isSentinel_tm(TM_ARG_ALONE, l)))) {
        gp = p;
        p = l;
        l = l->getChild_tm(TM_ARG_ALONE, key, cmp);
    }

    if (!(TM_SHARED_READ_L(l->tag) || (l->getABDegree_tm(TM_ARG_ALONE) < MIN_DEGREE && !isSentinel_tm(TM_ARG_ALONE, l)))) {
        TM_END();
        this->recordmgr->enterQuiescentState(tid);
        return true; // no violations to fix
    }

    // tag operations take precedence
    if (TM_SHARED_READ_L(l->tag)) {
        if (p->getABDegree_tm(TM_ARG_ALONE) + l->getABDegree_tm(TM_ARG_ALONE) <= DEGREE+1) {
            tagJoinParent_tm(TM_ARG_ALONE, tid, gp, p, gp->getChildIndex_tm(TM_ARG_ALONE, key, cmp), l, p->getChildIndex_tm(TM_ARG_ALONE, key, cmp));
        } else {
            tagSplit_tm(TM_ARG_ALONE, tid, gp, p, gp->getChildIndex_tm(TM_ARG_ALONE, key, cmp), l, p->getChildIndex_tm(TM_ARG_ALONE, key, cmp));
        }
    } else { // assert (l->getABDegree_tm(TM_ARG_ALONE) < MIN_DEGREE)
        // get sibling of l
        abtree_Node<DEGREE,K> * s;
        int lindex = p->getChildIndex_tm(TM_ARG_ALONE, key, cmp);
        int sindex = lindex ? lindex-1 : lindex+1;
        s = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(p->ptrs[sindex]);

        // tag operations take precedence
        if (TM_SHARED_READ_L(s->tag)) {
            if (p->getABDegree_tm(TM_ARG_ALONE) + s->getABDegree_tm(TM_ARG_ALONE) <= DEGREE+1) {
                tagJoinParent_tm(TM_ARG_ALONE, tid, gp, p, gp->getChildIndex_tm(TM_ARG_ALONE, key, cmp), s, sindex);
            } else {
                tagSplit_tm(TM_ARG_ALONE, tid, gp, p, gp->getChildIndex_tm(TM_ARG_ALONE, key, cmp), s, sindex);
            }
        } else {
            // either join l and s, or redistribute keys between them
            if (l->getABDegree_tm(TM_ARG_ALONE) + s->getABDegree_tm(TM_ARG_ALONE) < 2*MIN_DEGREE) {
                joinSibling_tm(TM_ARG_ALONE, tid, gp, p, gp->getChildIndex_tm(TM_ARG_ALONE, key, cmp), l, lindex, s, sindex);
            } else {
                redistributeSibling_tm(TM_ARG_ALONE, tid, gp, p, gp->getChildIndex_tm(TM_ARG_ALONE, key, cmp), l, lindex, s, sindex);
            }
        }
    }
    return false;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::rootJoinParent_tm(TM_ARGDECL_ALONE, const int tid, abtree_Node<DEGREE,K> * const p, abtree_Node<DEGREE,K> * const l, const int lindex) {
    abtree_Node<DEGREE,K> * c = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(l->ptrs[0]);
    abtree_Node<DEGREE,K> * newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
    newNode->marked = false;
    newNode->scxRecord = dummy;

    for (int i=0;i<c->getKeyCount_tm(TM_ARG_ALONE);++i) {
        newNode->keys[i] = TM_SHARED_READ_L(c->keys[i]);
    }
    for (int i=0;i<c->getABDegree_tm(TM_ARG_ALONE);++i) {
        newNode->ptrs[i] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(c->ptrs[i]);
    }

    newNode->tag = false; // since p is root(holder), newNode is the new actual root, so its tag is false
    newNode->size = TM_SHARED_READ_L(c->size);
    newNode->leaf = TM_SHARED_READ_L(c->leaf);

    TM_SHARED_WRITE_P(p->ptrs[lindex], newNode);
    TM_END();
    this->recordmgr->enterQuiescentState(tid);
    
    REPLACE_ALLOCATED_NODE(tid, 0);
    recordmgr->retire(tid, c);
    recordmgr->retire(tid, l);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::tagJoinParent_tm(TM_ARGDECL_ALONE, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex) {
    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->marked = false;
    newp->scxRecord = dummy;

    // elements of p left of l
    int k1=0, k2=0;
    for (int i=0;i<lindex;++i) {
        newp->keys[k1++] = TM_SHARED_READ_L(p->keys[i]);
    }
    for (int i=0;i<lindex;++i) {
        newp->ptrs[k2++] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(p->ptrs[i]);
    }

    // contents of l
    for (int i=0;i<l->getKeyCount_tm(TM_ARG_ALONE);++i) {
        newp->keys[k1++] = TM_SHARED_READ_L(l->keys[i]);
    }
    for (int i=0;i<l->getABDegree_tm(TM_ARG_ALONE);++i) {
        newp->ptrs[k2++] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(l->ptrs[i]);
    }

    // remaining elements of p
    for (int i=lindex;i<p->getKeyCount_tm(TM_ARG_ALONE);++i) {
        newp->keys[k1++] = TM_SHARED_READ_L(p->keys[i]);
    }
    // skip child pointer for lindex
    for (int i=lindex+1;i<p->getABDegree_tm(TM_ARG_ALONE);++i) {
        newp->ptrs[k2++] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(p->ptrs[i]);
    }
    
    newp->tag = false;
    newp->size = TM_SHARED_READ_L(p->size) + TM_SHARED_READ_L(l->size) - 1;
    newp->leaf = false;

    TM_SHARED_WRITE_P(gp->ptrs[pindex], newp);
    TM_END();
    this->recordmgr->enterQuiescentState(tid);
    
    REPLACE_ALLOCATED_NODE(tid, 0);
    recordmgr->retire(tid, l);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::tagSplit_tm(TM_ARGDECL_ALONE, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex) {
    // create new nodes for update
    const int sz = p->getABDegree_tm(TM_ARG_ALONE) + l->getABDegree_tm(TM_ARG_ALONE) - 1;
    const int leftsz = sz/2;
    const int rightsz = sz - leftsz;
    
    K keys[2*DEGREE+1];
    void * ptrs[2*DEGREE+1];
    int k1=0, k2=0;

    // elements of p left than l
    for (int i=0;i<lindex;++i) {
        keys[k1++] = TM_SHARED_READ_L(p->keys[i]);
    }
    for (int i=0;i<lindex;++i) {
        ptrs[k2++] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(p->ptrs[i]);
    }

    // contents of l
    for (int i=0;i<l->getKeyCount_tm(TM_ARG_ALONE);++i) {
        keys[k1++] = TM_SHARED_READ_L(l->keys[i]);
    }
    for (int i=0;i<l->getABDegree_tm(TM_ARG_ALONE);++i) {
        ptrs[k2++] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(l->ptrs[i]);
    }

    // remaining elements of p
    for (int i=lindex;i<p->getKeyCount_tm(TM_ARG_ALONE);++i) {
        keys[k1++] = TM_SHARED_READ_L(p->keys[i]);
    }
    // skip child pointer for lindex
    for (int i=lindex+1;i<p->getABDegree_tm(TM_ARG_ALONE);++i) {
        ptrs[k2++] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(p->ptrs[i]);
    }
    
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newleft = GET_ALLOCATED_NODE_PTR(tid, 1);
    newleft->scxRecord = dummy;
    newleft->marked = false;

    abtree_Node<DEGREE,K> * newright = GET_ALLOCATED_NODE_PTR(tid, 2);
    newright->scxRecord = dummy;
    newright->marked = false;

    k1=0;
    k2=0;
    
    for (int i=0;i<leftsz-1;++i) {
        newleft->keys[i] = keys[k1++];
    }
    for (int i=0;i<leftsz;++i) {
        newleft->ptrs[i] = ptrs[k2++];
    }
    newleft->tag = false;
    newleft->size = leftsz;
    newleft->leaf = false;
    
    newp->keys[0] = keys[k1++];
    newp->ptrs[0] = newleft;
    newp->ptrs[1] = newright;
    newp->tag = (gp != root);
    newp->size = 2;
    newp->leaf = false;
    
    for (int i=0;i<rightsz-1;++i) {
        newright->keys[i] = keys[k1++];
    }
    for (int i=0;i<rightsz;++i) {
        newright->ptrs[i] = ptrs[k2++];
    }
    newright->tag = false;
    newright->size = rightsz;
    newright->leaf = false;
    
    TM_SHARED_WRITE_P(gp->ptrs[pindex], newp);
    TM_END();
    this->recordmgr->enterQuiescentState(tid);
    
    recordmgr->retire(tid, l);
    recordmgr->retire(tid, p);
    REPLACE_ALLOCATED_NODE(tid, 0);
    REPLACE_ALLOCATED_NODE(tid, 1);
    REPLACE_ALLOCATED_NODE(tid, 2);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::joinSibling_tm(TM_ARGDECL_ALONE, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex) {
    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newl = GET_ALLOCATED_NODE_PTR(tid, 1);
    newl->scxRecord = dummy;
    newl->marked = false;

    // create newl by joining s to l

    abtree_Node<DEGREE,K> * left;
    abtree_Node<DEGREE,K> * right;
    int leftindex;
    int rightindex;
    if (lindex < sindex) {
        left = l;
        leftindex = lindex;
        right = s;
        rightindex = sindex;
    } else {
        left = s;
        leftindex = sindex;
        right = l;
        rightindex = lindex;
    }
    
    int k1=0, k2=0;
    for (int i=0;i<left->getKeyCount_tm(TM_ARG_ALONE);++i) {
        newl->keys[k1++] = TM_SHARED_READ_L(left->keys[i]);
    }
    for (int i=0;i<left->getABDegree_tm(TM_ARG_ALONE);++i) {
        newl->ptrs[k2++] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(left->ptrs[i]);
    }
    if (!left->isLeaf_tm(TM_ARG_ALONE)) newl->keys[k1++] = p->keys[leftindex];
    for (int i=0;i<right->getKeyCount_tm(TM_ARG_ALONE);++i) {
        newl->keys[k1++] = TM_SHARED_READ_L(right->keys[i]);
    }
    for (int i=0;i<right->getABDegree_tm(TM_ARG_ALONE);++i) {
        newl->ptrs[k2++] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(right->ptrs[i]);
    }
    
    // create newp from p by:
    // 1. skipping the key for leftindex and child pointer for sindex
    // 2. replacing l with newl
    for (int i=0;i<leftindex;++i) {
        newp->keys[i] = TM_SHARED_READ_L(p->keys[i]);
    }
    for (int i=0;i<sindex;++i) {
        newp->ptrs[i] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(p->ptrs[i]);
    }
    for (int i=leftindex+1;i<p->getKeyCount_tm(TM_ARG_ALONE);++i) {
        newp->keys[i-1] = TM_SHARED_READ_L(p->keys[i]);
    }
    for (int i=sindex+1;i<p->getABDegree_tm(TM_ARG_ALONE);++i) {
        newp->ptrs[i-1] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(p->ptrs[i]);
    }
    // replace l with newl
    newp->ptrs[lindex - (lindex > sindex)] = newl;
    
    newp->tag = false;
    newp->size = TM_SHARED_READ_L(p->size) - 1;
    newp->leaf = false;
    newl->tag = false;
    newl->size = TM_SHARED_READ_L(l->size) + TM_SHARED_READ_L(s->size);
    newl->leaf = TM_SHARED_READ_L(l->leaf);

    TM_SHARED_WRITE_P(gp->ptrs[pindex], newp);
    TM_END();
    this->recordmgr->enterQuiescentState(tid);
    
    REPLACE_ALLOCATED_NODE(tid, 0);
    REPLACE_ALLOCATED_NODE(tid, 1);
    recordmgr->retire(tid, p);
    recordmgr->retire(tid, l);
    recordmgr->retire(tid, s);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::redistributeSibling_tm(TM_ARGDECL_ALONE, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex) {
    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newl = GET_ALLOCATED_NODE_PTR(tid, 1);
    newl->scxRecord = dummy;
    newl->marked = false;

    abtree_Node<DEGREE,K> * news = GET_ALLOCATED_NODE_PTR(tid, 2);
    news->scxRecord = dummy;
    news->marked = false;

    // create newl and news by evenly sharing the keys + pointers of l and s
    int sz = l->getABDegree_tm(TM_ARG_ALONE) + s->getABDegree_tm(TM_ARG_ALONE);
    int leftsz = sz/2;
    int rightsz = sz-leftsz;
    kvpair<K> tosort[2*DEGREE+1];
    
    abtree_Node<DEGREE,K> * left;
    abtree_Node<DEGREE,K> * right;
    abtree_Node<DEGREE,K> * newleft;
    abtree_Node<DEGREE,K> * newright;
    int leftindex;
    int rightindex;
    if (lindex < sindex) {
        left = l;
        newleft = newl;
        leftindex = lindex;
        right = s;
        newright = news;
        rightindex = sindex;
    } else {
        left = s;
        newleft = news;
        leftindex = sindex;
        right = l;
        newright = newl;
        rightindex = lindex;
    }
    assert(rightindex == 1+leftindex);
    
    // combine the contents of l and s (and one key from p)
    int k1=0, k2=0;
    for (int i=0;i<left->getKeyCount_tm(TM_ARG_ALONE);++i) {
        tosort[k1++].key = TM_SHARED_READ_L(left->keys[i]);
    }
    for (int i=0;i<left->getABDegree_tm(TM_ARG_ALONE);++i) {
        tosort[k2++].val = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(left->ptrs[i]);
    }
    if (!left->isLeaf_tm(TM_ARG_ALONE)) tosort[k1++].key = p->keys[leftindex];
    for (int i=0;i<right->getKeyCount_tm(TM_ARG_ALONE);++i) {
        tosort[k1++].key = TM_SHARED_READ_L(right->keys[i]);
    }
    for (int i=0;i<right->getABDegree_tm(TM_ARG_ALONE);++i) {
        tosort[k2++].val = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(right->ptrs[i]);
    }
    
    // sort if this is a leaf
    if (left->isLeaf_tm(TM_ARG_ALONE)) qsort(tosort, k1, sizeof(kvpair<K>), kv_compare<K,Compare>);
    
    // distribute contents between newleft and newright
    k1=0;
    k2=0;
    for (int i=0;i<leftsz - !left->isLeaf_tm(TM_ARG_ALONE);++i) {
        newleft->keys[i] = tosort[k1++].key;
    }
    for (int i=0;i<leftsz;++i) {
        newleft->ptrs[i] = tosort[k2++].val;
    }
    // reserve one key for the parent (to go between newleft and newright))
    K keyp = tosort[k1].key;
    if (!left->isLeaf_tm(TM_ARG_ALONE)) ++k1;
    for (int i=0;i<rightsz - !left->isLeaf_tm(TM_ARG_ALONE);++i) {
        newright->keys[i] = tosort[k1++].key;
    }
    for (int i=0;i<rightsz;++i) {
        newright->ptrs[i] = tosort[k2++].val;
    }
    
    // create newp from p by replacing left with newleft and right with newright,
    // and replacing one key (between these two pointers)
    for (int i=0;i<p->getKeyCount_tm(TM_ARG_ALONE);++i) {
        newp->keys[i] = TM_SHARED_READ_L(p->keys[i]);
    }
    for (int i=0;i<p->getABDegree_tm(TM_ARG_ALONE);++i) {
        newp->ptrs[i] = (abtree_Node<DEGREE,K> *) TM_SHARED_READ_P(p->ptrs[i]);
    }
    newp->keys[leftindex] = keyp;
    newp->ptrs[leftindex] = newleft;
    newp->ptrs[rightindex] = newright;
    newp->tag = false;
    newp->size = TM_SHARED_READ_L(p->size);
    newp->leaf = false;
    
    newleft->tag = false;
    newleft->size = leftsz;
    newleft->leaf = TM_SHARED_READ_L(left->leaf);
    newright->tag = false;
    newright->size = rightsz;
    newright->leaf = TM_SHARED_READ_L(right->leaf);

    TM_SHARED_WRITE_P(gp->ptrs[pindex], newp);
    TM_END();
    this->recordmgr->enterQuiescentState(tid);

    REPLACE_ALLOCATED_NODE(tid, 0);
    REPLACE_ALLOCATED_NODE(tid, 1);
    REPLACE_ALLOCATED_NODE(tid, 2);
    recordmgr->retire(tid, l);
    recordmgr->retire(tid, s);
    recordmgr->retire(tid, p);
    return true;
}
#endif








#define THREE_PATH_BEGIN(info) \
    info.path = (MAX_FAST_HTM_RETRIES >= 0 ? PATH_FAST_HTM : MAX_SLOW_HTM_RETRIES >= 0 ? PATH_SLOW_HTM : PATH_FALLBACK); \
/*    info.path = (MAX_FAST_HTM_RETRIES >= 0*/ \
/*            ? (numFallback > 0*/ \
/*                    ? (MAX_SLOW_HTM_RETRIES >= 0 ? PATH_SLOW_HTM : PATH_FALLBACK)*/ \
/*                    : PATH_FAST_HTM)*/ \
/*            : MAX_SLOW_HTM_RETRIES >= 0 ? PATH_SLOW_HTM : PATH_FALLBACK);*/ \
    int attempts = 0; \
    for (;;) { \
        recordmgr->leaveQuiescentState(tid);

#define THREE_PATH_END(info, finished, countersSucc, countersFail, countersAbort) \
        recordmgr->enterQuiescentState(tid); \
        ++attempts; \
        if (finished) { \
            if ((info.path == PATH_FALLBACK) && (MAX_FAST_HTM_RETRIES >= 0 || MAX_SLOW_HTM_RETRIES >= 0)) { \
                __sync_fetch_and_add(&numFallback, -1); \
            } \
            counters->pathFail[info.path]->add(tid, attempts-1); \
            counters->pathSuccess[info.path]->inc(tid); \
            break; \
        } \
        switch (info.path) { \
            case PATH_FAST_HTM: \
                /* check if we should change paths */ \
                if (attempts > MAX_FAST_HTM_RETRIES) { \
                    counters->pathFail[info.path]->add(tid, attempts); \
                    attempts = 0; \
                    if (MAX_SLOW_HTM_RETRIES < 0) { \
                        info.path = PATH_FALLBACK; \
                        __sync_fetch_and_add(&numFallback, 1); \
                    } else { \
                        info.path = PATH_SLOW_HTM; \
                    } \
                /* MOVE TO THE MIDDLE PATH IMMEDIATELY IF SOMEONE IS ON THE FALLBACK PATH */ \
                } else if ((info.lastAbort >> 24) == ABORT_PROCESS_ON_FALLBACK && MAX_SLOW_HTM_RETRIES >= 0) { \
                    attempts = 0; \
                    info.path = PATH_SLOW_HTM; \
                /* if there is no middle path, wait for the fallback path to be empty */ \
                } else if (MAX_SLOW_HTM_RETRIES < 0) { \
                    while (numFallback > 0) { __asm__ __volatile__("pause;"); } \
                } \
                break; \
            case PATH_SLOW_HTM: \
                /* check if we should change paths */ \
                if (attempts > MAX_SLOW_HTM_RETRIES) { \
                    counters->pathFail[info.path]->add(tid, attempts); \
                    attempts = 0; \
                    info.path = PATH_FALLBACK; \
                    __sync_fetch_and_add(&numFallback, 1); \
                } \
                break; \
            case PATH_FALLBACK: { \
                /** BEGIN DEBUG **/ \
                const int MAX_ATTEMPTS = 1000000; \
                if (attempts == MAX_ATTEMPTS) { cout<<"ERROR: more than "<<MAX_ATTEMPTS<<" attempts on fallback"<<endl; TRACE_ON; } \
                if (attempts > 2*MAX_ATTEMPTS) { cout<<"ERROR: more than "<<(2*MAX_ATTEMPTS)<<" attempts on fallback"<<endl; this->debugPrint(); exit(-1); } \
                /** END DEBUG **/ \
                } \
                break; \
            default: \
                cout<<"reached impossible switch case"<<endl; \
                exit(-1); \
                break; \
        } \
    }

template<int DEGREE, typename K, class Compare, class RecManager>
const pair<void*,bool> abtree<DEGREE,K,Compare,RecManager>::find(const int tid, const K& key) {
    pair<void*,bool> result;
    this->recordmgr->leaveQuiescentState(tid);
    abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
    while (!l->isLeaf()) {
        l = l->getChild(key, cmp);
    }
    int index = l->getKeyIndex(key, cmp);
    if (index < l->getKeyCount()) {
        result.first = l->ptrs[index];
        result.second = true;
    } else {
        result.first = NO_VALUE;
        result.second = false;
    }
    this->recordmgr->enterQuiescentState(tid);
    return result;
}

template<int DEGREE, typename K, class Compare, class RecManager>
int abtree<DEGREE,K,Compare,RecManager>::rangeQuery(const int tid, const K& lo, const K& hi, abtree_Node<DEGREE,K> const ** result) {
    bool onlyIfAbsent = false;
    bool shouldRebalance = false;
    int cnt;
    
    // do insert
    wrapper_info<DEGREE,K> info;
    bool retval = false;
//    cout<<"EXECUTED RANGE QUERY\n";

    THREE_PATH_BEGIN(info);
        if (info.path == PATH_FAST_HTM) {
            retval = rangeQuery_fast(&info, tid, lo, hi, result, &cnt);
        } else if (info.path == PATH_SLOW_HTM) {
            retval = rangeQuery_fallback(&info, tid, lo, hi, result, &cnt);
        } else /*if (info.path == PATH_FALLBACK)*/ {
            retval = rangeQuery_fallback(&info, tid, lo, hi, result, &cnt);
        }
    THREE_PATH_END(info, retval, counters->pathSuccess, counters->pathFail, counters->htmAbort);
    
    return cnt;
}

template<int DEGREE, typename K, class Compare, class RecManager>
const void* abtree<DEGREE,K,Compare,RecManager>::insert(const int tid, const K& key, void * const val) {
    bool onlyIfAbsent = false;
    bool shouldRebalance = false;
    void * result = NO_VALUE;
    
    // do insert
    wrapper_info<DEGREE,K> info;
    bool retval = false;
    THREE_PATH_BEGIN(info);
        if (info.path == PATH_FAST_HTM) {
            retval = insert_fast(&info, tid, key, val, onlyIfAbsent, &shouldRebalance, &result);
//            if (!retval) cout<<"RETVAL WAS FALSE IN INSERT\n";
        } else if (info.path == PATH_SLOW_HTM) {
            retval = insert_middle(&info, tid, key, val, onlyIfAbsent, &shouldRebalance, &result);
//            cout<<"EXECUTED MIDDLE IN INSERT\n";
        } else /*if (info.path == PATH_FALLBACK)*/ {
            retval = insert_fallback(&info, tid, key, val, onlyIfAbsent, &shouldRebalance, &result);
        }
    THREE_PATH_END(info, retval, counters->pathSuccess, counters->pathFail, counters->htmAbort);

    // do rebalancing
    while (shouldRebalance) {
        info = wrapper_info<DEGREE,K>();
        THREE_PATH_BEGIN(info);
            if (info.path == PATH_FAST_HTM) {
                retval = rebalance_fast(&info, tid, key, &shouldRebalance);
            } else if (info.path == PATH_SLOW_HTM) {
                retval = rebalance_middle(&info, tid, key, &shouldRebalance);
            } else /*if (info.path == PATH_FALLBACK)*/ {
                retval = rebalance_fallback(&info, tid, key, &shouldRebalance);
            }
        THREE_PATH_END(info, retval, counters->pathSuccess, counters->pathFail, counters->htmAbort);
    }
    return result;
}

template<int DEGREE, typename K, class Compare, class RecManager>
const pair<void *,bool> abtree<DEGREE,K,Compare,RecManager>::erase(const int tid, const K& key) {
    bool shouldRebalance = false;
    void * result = NO_VALUE;

    // do erase
    wrapper_info<DEGREE,K> info;
    bool retval = false;
    THREE_PATH_BEGIN(info);
        if (info.path == PATH_FAST_HTM) {
            retval = erase_fast(&info, tid, key, &shouldRebalance, &result);
//            if (!retval) cout<<"RETVAL WAS FALSE IN ERASE\n";
        } else if (info.path == PATH_SLOW_HTM) {
            retval = erase_middle(&info, tid, key, &shouldRebalance, &result);
//            cout<<"EXECUTED MIDDLE IN ERASE\n";
        } else /*if (info.path == PATH_FALLBACK)*/ {
            retval = erase_fallback(&info, tid, key, &shouldRebalance, &result);
        }
    THREE_PATH_END(info, retval, counters->pathSuccess, counters->pathFail, counters->htmAbort);

    // do rebalancing
    while (shouldRebalance) {
        info = wrapper_info<DEGREE,K>();
        THREE_PATH_BEGIN(info);
            if (info.path == PATH_FAST_HTM) {
                retval = rebalance_fast(&info, tid, key, &shouldRebalance);
            } else if (info.path == PATH_SLOW_HTM) {
                retval = rebalance_middle(&info, tid, key, &shouldRebalance);
            } else /*if (infopath == PATH_FALLBACK)*/ {
                retval = rebalance_fallback(&info, tid, key, &shouldRebalance);
            }
        THREE_PATH_END(info, retval, counters->pathSuccess, counters->pathFail, counters->htmAbort);
    }
    return pair<void *,bool>(result, result != NO_VALUE);
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::rangeQuery_fast(wrapper_info<DEGREE,K> * const info, const int tid, const K& lo, const K& hi, abtree_Node<DEGREE,K> const ** result, int * const cnt) {
    block<abtree_Node<DEGREE,K> > stack (NULL);
    *cnt = 0;
    
//    this->recordmgr->leaveQuiescentState(tid);
    
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        // depth first traversal (of interesting subtrees)
        stack.push(root);
        while (!stack.isEmpty()) {
            abtree_Node<DEGREE,K> * node = stack.pop();

            // if internal node, explore its children
            if (!node->isLeaf()) {
                // find right-most sub-tree that could contain a key in [lo, hi]
                int nkeys = node->getKeyCount();
                int r = nkeys;
                while (r > 0 && cmp(hi, (const K&) node->keys[r-1])) { // subtree rooted at u.c.get(r) contains only keys > hi
                    --r;
                }
                // find left-most sub-tree that could contain a key in [lo, hi]
                int l = 0;
                while (l < nkeys && !cmp(lo, (const K&) node->keys[l])) {
                    ++l;
                }
                // perform DFS from left to right (so push onto stack from right to left)
                for (int i=r;i>=l; --i) {
                    stack.push((abtree_Node<DEGREE,K> *) node->ptrs[i]);
                }

            // else if leaf node, add it to the result that will be returned
            } else {
                result[(*cnt)++] = node;
            }
        }
        XEND();
    } else {
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        this->counters->rqFail->inc(tid);
        if (info) info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
//        this->recordmgr->enterQuiescentState(tid);
        return false;
    }
    // success
//    this->recordmgr->enterQuiescentState(tid);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::rangeQuery_fallback(wrapper_info<DEGREE,K> * const info, const int tid, const K& lo, const K& hi, abtree_Node<DEGREE,K> const ** result, int * const cnt) {
    const int size = hi - lo + 1;
    TRACE COUTATOMICTID("rangeQuery(lo="<<lo<<", hi="<<hi<<", size="<<size<<")"<<endl);

    block<abtree_Node<DEGREE,K> > stack (NULL);
    *cnt = 0;
    
//    this->recordmgr->leaveQuiescentState(tid);

    // depth first traversal (of interesting subtrees)
    stack.push(root);
    while (!stack.isEmpty()) {
        abtree_Node<DEGREE,K> * node = stack.pop();
        void * children[DEGREE];
        
        //COUTATOMICTID("    visiting node "<<*node<<endl);
        // if llx on node fails, then retry
        if (llx(tid, node, children) == NULL) { // marked bit checked in here
//            this->counters->rqFail->inc(tid);
            //cout<<"Retry because of failed llx\n";
//            this->recordmgr->enterQuiescentState(tid);
            return false;

        // else if internal node, explore its children
        } else if (!node->isLeaf()) {
            //COUTATOMICTID("    internal node key="<<node->key<<" low="<<low<<" hi="<<hi<<" cmp(hi, node->key)="<<cmp(hi, node->key)<<" cmp(low, node->key)="<<cmp(low, node->key)<<endl);
            // find right-most sub-tree that could contain a key in [lo, hi]
            int nkeys = node->getKeyCount();
            int r = nkeys;
            while (r > 0 && cmp(hi, (const K&) node->keys[r-1])) { // subtree rooted at u.c.get(r) contains only keys > hi
                --r;
            }
            // find left-most sub-tree that could contain a key in [lo, hi]
            int l = 0;
            while (l < nkeys && !cmp(lo, (const K&) node->keys[l])) {
                ++l;
            }
            // perform DFS from left to right (so push onto stack from right to left)
            for (int i=r;i>=l; --i) {
                stack.push((abtree_Node<DEGREE,K> *) node->ptrs[i]);
            }
            
        // else if leaf node, add it to the result that will be returned
        } else {
            result[(*cnt)++] = node;
        }
    }
    // validation
    for (int i=0;i<*cnt;++i) {
        if (result[i]->marked) {
//            this->counters->rqFail->inc(tid);
            //cout<<"Retry because of failed validation, return set size "<<cnt<<endl;
//            this->recordmgr->enterQuiescentState(tid);
            return false;
        }
    }
    
    // success
//    this->recordmgr->enterQuiescentState(tid);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::insert_fast(
            wrapper_info<DEGREE,K> * const info, const int tid, const K& key, void * const val, const bool onlyIfAbsent, bool * const shouldRebalance, void ** const result) {
    abtree_Node<DEGREE,K> * p;
    abtree_Node<DEGREE,K> * l;
    int keyindexl;
    int lindex;
    int nkeysl;
    bool found;
    abtree_Node<DEGREE,K> * parent = GET_ALLOCATED_NODE_PTR(tid, 0);
    abtree_Node<DEGREE,K> * left = GET_ALLOCATED_NODE_PTR(tid, 1);
    abtree_Node<DEGREE,K> * right;
    kvpair<K> tosort[DEGREE+1];

    if (parent->marked) parent->marked = false;
    parent->scxRecord = dummy;
    if (left->marked) left->marked = false;
    left->scxRecord = dummy;

    int attempts = MAX_FAST_HTM_RETRIES;
TXN1: (0);
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        if (numFallback > 0) { XABORT(ABORT_PROCESS_ON_FALLBACK); }

        p = root;
        l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
        TXN_ASSERT(l);
        while (!l->isLeaf()) {
            p = l;
            l = l->getChild(key, cmp);
            TXN_ASSERT(l);
        }
        keyindexl = l->getKeyIndex(key, cmp);
        lindex = p->getChildIndex(key, cmp);
        nkeysl = l->getKeyCount();
   
        found = (keyindexl < nkeysl);
        if (found) {
            if (onlyIfAbsent) {
                *result = l->ptrs[keyindexl];
                XEND();
                *shouldRebalance = false;
                
                this->counters->htmCommit[info->path]->inc(tid);
                TRACE COUTATOMICTID("insert_fast: returned because onlyIfAbsent and key "<<key<<" was present"<<endl);
                return true;
            } else {
                // replace value
                *result = l->ptrs[keyindexl];
                l->ptrs[keyindexl] = val;
                XEND();
                *shouldRebalance = false;
                
                this->counters->htmCommit[info->path]->inc(tid);
//                this->counters->updateChange[info->path]->inc(tid);
                TRACE COUTATOMICTID("insert_fast: replaced value"<<endl);
                return true;
            }
        } else {
            if (nkeysl < DEGREE) {
                // inserting new key/value pair into leaf
                l->keys[nkeysl] = key;
                l->ptrs[nkeysl] = val;
                ++l->size;
                XEND();
//                __sync_synchronize();
                *result = NO_VALUE;
                *shouldRebalance = false;
                
                this->counters->htmCommit[info->path]->inc(tid);
//                this->counters->updateChange[info->path]->inc(tid);
                TRACE COUTATOMICTID("insert_fast: inserted new key/value pair into leaf"<<endl);
                return true;
            } else { // nkeysl == DEGREE
                // overflow: insert a new tagged parent above l and create a new sibling
                right = l;

                for (int i=0;i<nkeysl;++i) {
                    tosort[i].key = l->keys[i];
                    tosort[i].val = l->ptrs[i];
                }
                tosort[nkeysl].key = key;
                tosort[nkeysl].val = val;
                qsort(tosort, nkeysl+1, sizeof(kvpair<K>), kv_compare<K,Compare>);

                const int leftLength = (nkeysl+1)/2;
                for (int i=0;i<leftLength;++i) {
                    left->keys[i] = tosort[i].key;
                }
                for (int i=0;i<leftLength;++i) {
                    left->ptrs[i] = tosort[i].val;
                }
                left->tag = false;
                left->size = leftLength;
                left->leaf = true;

                const int rightLength = (nkeysl+1) - leftLength;
                for (int i=0;i<rightLength;++i) {
                    right->keys[i] = tosort[i+leftLength].key;
                }
                for (int i=0;i<rightLength;++i) {
                    right->ptrs[i] = tosort[i+leftLength].val;
                }
                right->size = rightLength;

                parent->keys[0] = right->keys[0];
                parent->ptrs[0] = left;
                parent->ptrs[1] = right;
                parent->tag = (p != root);
                parent->size = 2;
                parent->leaf = false;
                
                p->ptrs[lindex] = parent;
                XEND();
                
                *result = NO_VALUE;
                *shouldRebalance = true;
                
                this->counters->htmCommit[info->path]->inc(tid);
//                this->counters->updateChange[info->path]->inc(tid);
                TRACE COUTATOMICTID("insert_fast: overflow: inserted a new tagged parent above l and create a new sibling"<<endl);
                
                // do memory reclamation and allocation
                REPLACE_ALLOCATED_NODE(tid, 0);
                REPLACE_ALLOCATED_NODE(tid, 1);
                
                return true;
            }
        }
    } else {
aborthere:
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
        if (info) info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        TRACE COUTATOMICTID("insert_fast: transaction aborted"<<endl);
        return false;
    }
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::insert_middle(
            wrapper_info<DEGREE,K> * const info, const int tid, const K& key, void * const val, const bool onlyIfAbsent, bool * const shouldRebalance, void ** const result) {
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        abtree_Node<DEGREE,K> * p = root;
        abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
        while (!l->isLeaf()) {
            TXN_ASSERT(l->ptrs[0] != NULL);
            p = l;
            l = l->getChild(key, cmp);
        }

        void * ptrsp[DEGREE];
        if ((info->scxRecordsSeen[0] = (abtree_SCXRecord<DEGREE,K> *) llx_txn(tid, p, ptrsp)) == NULL) { /*TRACE COUTATOMICTID("returned because LLX failed on p"<<endl);*/ XABORT(ABORT_LLX_FAILED); }
        int lindex = p->getChildIndex(key, cmp);
        if (p->ptrs[lindex] != l) { /*TRACE COUTATOMICTID("returned because p->ptrs[lindex] != l"<<endl);*/ XABORT(ABORT_NODE_POINTER_CHANGED); }
        info->nodes[0] = p;

        void * ptrsl[DEGREE];
        if ((info->scxRecordsSeen[1] = (abtree_SCXRecord<DEGREE,K> *) llx_txn(tid, l, ptrsl)) == NULL) { /*TRACE COUTATOMICTID("returned because LLX failed on l"<<endl);*/ XABORT(ABORT_LLX_FAILED); }
        int keyindexl = l->getKeyIndex(key, cmp);
        info->nodes[1] = l;

        int nkeysl = l->getKeyCount();
        bool found = (keyindexl < nkeysl);
        if (found) {
            if (onlyIfAbsent) {
                *result = l->ptrs[keyindexl];
                XEND();
                *shouldRebalance = false;
                //TRACE COUTATOMICTID("returned because onlyIfAbsent and key "<<key<<" was present"<<endl);
                return true;
            } else {
                //TRACE COUTATOMICTID("replacing leaf with a new copy that has the new value instead of the old one"<<endl);
                // replace leaf with a new copy that has the new value instead of the old one
                abtree_Node<DEGREE,K> * newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
                newNode->marked = false;
                newNode->scxRecord = dummy;

                for (int i=0;i<nkeysl;++i) {
                    newNode->keys[i] = l->keys[i];
                }
                for (int i=0;i<nkeysl;++i) {
                    newNode->ptrs[i] = l->ptrs[i];
                }
                newNode->ptrs[keyindexl] = val;

                newNode->tag = false;
                newNode->leaf = true;
                newNode->size = l->size;

                info->numberOfNodesAllocated = 1;
                info->numberOfNodesToFreeze = 2;
                info->numberOfNodes = 2;
                info->field = &p->ptrs[lindex];
                info->newNode = newNode;

                *result = l->ptrs[keyindexl];
                *shouldRebalance = false;
            }
        } else {
            if (nkeysl < DEGREE) {
                //TRACE COUTATOMICTID("replacing leaf with a new copy that has the new key/value pair inserted"<<endl);
                // replace leaf with a new copy that has the new key/value pair inserted
                abtree_Node<DEGREE,K> * newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
                newNode->marked = false;
                newNode->scxRecord = dummy;

                for (int i=0;i<nkeysl;++i) {
                    newNode->keys[i] = l->keys[i];
                }
                for (int i=0;i<nkeysl;++i) {
                    newNode->ptrs[i] = l->ptrs[i];
                }
                newNode->keys[nkeysl] = key;
                newNode->ptrs[nkeysl] = val;

                newNode->tag = false;
                newNode->leaf = true;
                newNode->size = nkeysl+1;

                info->numberOfNodesAllocated = 1;
                info->numberOfNodesToFreeze = 2;
                info->numberOfNodes = 2;
                info->field = &p->ptrs[lindex];
                info->newNode = newNode;

                *result = NO_VALUE;
                *shouldRebalance = false;
            } else { // nkeysl == DEGREE
                //TRACE COUTATOMICTID("overflow: replace the leaf with a subtree of three nodes"<<endl);
                // overflow: replace the leaf with a subtree of three nodes
                abtree_Node<DEGREE,K> * parent = GET_ALLOCATED_NODE_PTR(tid, 0);
                parent->marked = false;
                parent->scxRecord = dummy;

                abtree_Node<DEGREE,K> * left = GET_ALLOCATED_NODE_PTR(tid, 1);
                left->marked = false;
                left->scxRecord = dummy;

                abtree_Node<DEGREE,K> * right = GET_ALLOCATED_NODE_PTR(tid, 2);
                right->marked = false;
                right->scxRecord = dummy;

                kvpair<K> tosort[nkeysl+1];
                for (int i=0;i<nkeysl;++i) {
                    tosort[i].key = l->keys[i];
                    tosort[i].val = l->ptrs[i];
                }
                tosort[nkeysl].key = key;
                tosort[nkeysl].val = val;
                qsort(tosort, nkeysl+1, sizeof(kvpair<K>), kv_compare<K,Compare>);

                const int leftLength = (nkeysl+1)/2;
                //TRACE COUTATOMICTID("leftLength = "<<leftLength<<endl);
                for (int i=0;i<leftLength;++i) {
                    left->keys[i] = tosort[i].key;
                }
                for (int i=0;i<leftLength;++i) {
                    left->ptrs[i] = tosort[i].val;
                }
                left->tag = false;
                left->size = leftLength;
                left->leaf = true;

                const int rightLength = (nkeysl+1) - leftLength;
                for (int i=0;i<rightLength;++i) {
                    right->keys[i] = tosort[i+leftLength].key;
                }
                for (int i=0;i<rightLength;++i) {
                    right->ptrs[i] = tosort[i+leftLength].val;
                }
                right->tag = false;
                right->size = rightLength;
                right->leaf = true;

                parent->keys[0] = right->keys[0];
                parent->ptrs[0] = left;
                parent->ptrs[1] = right;
                parent->tag = (p != root);
                parent->size = 2;
                parent->leaf = false;

                info->numberOfNodesAllocated = 3;
                info->numberOfNodesToFreeze = 2;
                info->numberOfNodes = 2;
                info->field = &p->ptrs[lindex];
                info->newNode = parent;

                *result = NO_VALUE;
                *shouldRebalance = true;
            }
        }

        bool retval = scx_txn(tid, info);
        XEND();
        reclaimMemoryAfterSCX(tid, info, true);
        if (retval) {
//            this->counters->updateChange[info->path]->inc(tid);
            return true;
        }
        return false;
    } else {
aborthere:
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
#ifdef NO_TXNS
        cout<<"aborted insert_middle with status code "<<status;
        long long compressed = getCompressedStatus(status);
        cout<<" ="<<getAutomaticAbortNames(compressed)<<getExplicitAbortName(compressed)<<endl;
        exit(-1);
#endif
        if (info) info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        return false;
    }
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::insert_fallback(
            wrapper_info<DEGREE,K> * const info, const int tid, const K& key, void * const val, const bool onlyIfAbsent, bool * const shouldRebalance, void ** const result) {
    abtree_Node<DEGREE,K> * p = root;
    abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
    while (!l->isLeaf()) {
        p = l;
        l = l->getChild(key, cmp);
    }
    
    void * ptrsp[DEGREE];
    if ((info->scxRecordsSeen[0] = (abtree_SCXRecord<DEGREE,K> *) llx(tid, p, ptrsp)) == NULL) { TRACE COUTATOMICTID("returned because LLX failed on p"<<endl); return false; }
    int lindex = p->getChildIndex(key, cmp);
    if (p->ptrs[lindex] != l) { TRACE COUTATOMICTID("returned because p->ptrs[lindex] != l"<<endl); return false; }
    info->nodes[0] = p;
    
    void * ptrsl[DEGREE];
    if ((info->scxRecordsSeen[1] = (abtree_SCXRecord<DEGREE,K> *) llx(tid, l, ptrsl)) == NULL) { TRACE COUTATOMICTID("returned because LLX failed on l"<<endl); return false; }
    int keyindexl = l->getKeyIndex(key, cmp);
    info->nodes[1] = l;
    
//    TRACE COUTATOMICTID("info->nodes[0]->isLeaf() = "<<info->nodes[0]->isLeaf()<<endl);
//    TRACE COUTATOMICTID("info->scxRecordsSeen[0] = "<<(long long) info->scxRecordsSeen[0]<<endl);

    int nkeysl = l->getKeyCount();
    bool found = (keyindexl < nkeysl);
    if (found) {
        if (onlyIfAbsent) {
            *result = l->ptrs[keyindexl];
            *shouldRebalance = false;
            TRACE COUTATOMICTID("insert_fallback: returned because onlyIfAbsent and key "<<key<<" was present"<<endl);
            return true;
        } else {
            TRACE COUTATOMICTID("insert_fallback: replacing leaf with a new copy that has the new value instead of the old one"<<endl);
            // replace leaf with a new copy that has the new value instead of the old one
            abtree_Node<DEGREE,K> * newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
            newNode->marked = false;
            newNode->scxRecord = dummy;

            for (int i=0;i<nkeysl;++i) {
                newNode->keys[i] = l->keys[i];
            }
            for (int i=0;i<nkeysl;++i) {
                newNode->ptrs[i] = l->ptrs[i];
            }
            newNode->ptrs[keyindexl] = val;

            newNode->tag = false;
            newNode->leaf = true;
            newNode->size = l->size;
            
            info->numberOfNodesAllocated = 1;
            info->numberOfNodesToFreeze = 2;
            info->numberOfNodes = 2;
            info->field = &p->ptrs[lindex];
            info->newNode = newNode;
            
            *result = l->ptrs[keyindexl];
            *shouldRebalance = false;
        }
    } else {
        if (nkeysl < DEGREE) {
            TRACE COUTATOMICTID("insert_fallback: replacing leaf with a new copy that has the new key/value pair inserted"<<endl);
            // replace leaf with a new copy that has the new key/value pair inserted
            abtree_Node<DEGREE,K> * newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
            newNode->marked = false;
            newNode->scxRecord = dummy;

            for (int i=0;i<nkeysl;++i) {
                newNode->keys[i] = l->keys[i];
            }
            for (int i=0;i<nkeysl;++i) {
                newNode->ptrs[i] = l->ptrs[i];
            }
            newNode->keys[nkeysl] = key;
            newNode->ptrs[nkeysl] = val;

            newNode->tag = false;
            newNode->leaf = true;
            newNode->size = nkeysl+1;
            
            info->numberOfNodesAllocated = 1;
            info->numberOfNodesToFreeze = 2;
            info->numberOfNodes = 2;
            info->field = &p->ptrs[lindex];
            info->newNode = newNode;
            
            *result = NO_VALUE;
            *shouldRebalance = false;
        } else { // nkeysl == DEGREE
            TRACE COUTATOMICTID("insert_fallback: overflow: replace the leaf with a subtree of three nodes"<<endl);
            // overflow: replace the leaf with a subtree of three nodes
            abtree_Node<DEGREE,K> * parent = GET_ALLOCATED_NODE_PTR(tid, 0);
            parent->marked = false;
            parent->scxRecord = dummy;

            abtree_Node<DEGREE,K> * left = GET_ALLOCATED_NODE_PTR(tid, 1);
            left->marked = false;
            left->scxRecord = dummy;

            abtree_Node<DEGREE,K> * right = GET_ALLOCATED_NODE_PTR(tid, 2);
            right->marked = false;
            right->scxRecord = dummy;

            kvpair<K> tosort[nkeysl+1];
            for (int i=0;i<nkeysl;++i) {
                tosort[i].key = l->keys[i];
                tosort[i].val = l->ptrs[i];
            }
            tosort[nkeysl].key = key;
            tosort[nkeysl].val = val;
            qsort(tosort, nkeysl+1, sizeof(kvpair<K>), kv_compare<K,Compare>);
            
            const int leftLength = (nkeysl+1)/2;
            //TRACE COUTATOMICTID("leftLength = "<<leftLength<<endl);
            for (int i=0;i<leftLength;++i) {
                left->keys[i] = tosort[i].key;
            }
            for (int i=0;i<leftLength;++i) {
                left->ptrs[i] = tosort[i].val;
            }
            left->tag = false;
            left->size = leftLength;
            left->leaf = true;
            
            const int rightLength = (nkeysl+1) - leftLength;
            for (int i=0;i<rightLength;++i) {
                right->keys[i] = tosort[i+leftLength].key;
            }
            for (int i=0;i<rightLength;++i) {
                right->ptrs[i] = tosort[i+leftLength].val;
            }
            right->tag = false;
            right->size = rightLength;
            right->leaf = true;
            
            parent->keys[0] = right->keys[0];
            parent->ptrs[0] = left;
            parent->ptrs[1] = right;
            parent->tag = (p != root);
            parent->size = 2;
            parent->leaf = false;
            
            info->numberOfNodesAllocated = 3;
            info->numberOfNodesToFreeze = 2;
            info->numberOfNodes = 2;
            info->field = &p->ptrs[lindex];
            info->newNode = parent;
            
            *result = NO_VALUE;
            *shouldRebalance = true;
        }
    }
    
    bool retval = scx(tid, info);
    reclaimMemoryAfterSCX(tid, info, false);
    if (retval) {
//        this->counters->updateChange[info->path]->inc(tid);
        TRACE COUTATOMICTID("insert_fallback: SCX succeeded"<<endl);
        return true;
    }
    TRACE COUTATOMICTID("insert_fallback: SCX FAILED."<<endl);
    return false;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::erase_fast(
            wrapper_info<DEGREE,K> * const info, const int tid, const K& key, bool * const shouldRebalance, void ** const result) {
    abtree_Node<DEGREE,K> * p;
    abtree_Node<DEGREE,K> * l;
    int keyindexl;
    int nkeysl;
    bool found;
    
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        if (numFallback > 0) { XABORT(ABORT_PROCESS_ON_FALLBACK); }

        p = root;
        l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
        TXN_ASSERT(l);
        while (!l->isLeaf()) {
            p = l;
            l = l->getChild(key, cmp);
            TXN_ASSERT(l);
        }

        keyindexl = l->getKeyIndex(key, cmp);
        nkeysl = l->getKeyCount();
        
        found = (keyindexl < nkeysl);
        if (!found) {
            XEND();
            *result = NO_VALUE;
            *shouldRebalance = false;

            this->counters->htmCommit[info->path]->inc(tid);
            TRACE COUTATOMICTID("erase_fast: key was not found"<<endl);
            return true;
        } else {
            
            // delete key/value pair from leaf
            *result = l->ptrs[keyindexl];
            l->keys[keyindexl] = l->keys[nkeysl-1];
            l->ptrs[keyindexl] = l->ptrs[nkeysl-1];
            --l->size;
            XEND();
            *shouldRebalance = (nkeysl < MIN_DEGREE);
            
            this->counters->htmCommit[info->path]->inc(tid);
//            this->counters->updateChange[info->path]->inc(tid);
            TRACE COUTATOMICTID("erase_fast: removed key/value pair from leaf"<<endl);
            return true;
        }
    } else {
aborthere:
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
        if (info) info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        TRACE COUTATOMICTID("erase_fast: transaction aborted"<<endl);
        return false;
    }
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::erase_middle(
            wrapper_info<DEGREE,K> * const info, const int tid, const K& key, bool * const shouldRebalance, void ** const result) {
    int status;
    abtree_Node<DEGREE,K> * p;
    abtree_Node<DEGREE,K> * l;
    abtree_Node<DEGREE,K> * newNode;
    void * ptrsp[DEGREE];
    int lindex;
    void * ptrsl[DEGREE];
    int keyindexl;
    int nkeysl;
    bool found;
    bool retval;
    
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        p = root;
        l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
        while (!l->isLeaf()) {
            TXN_ASSERT(l->ptrs[0] != NULL);
            p = l;
            l = l->getChild(key, cmp);
        }

        if ((info->scxRecordsSeen[0] = (abtree_SCXRecord<DEGREE,K> *) llx_txn(tid, p, ptrsp)) == NULL) XABORT(ABORT_LLX_FAILED);
        lindex = p->getChildIndex(key, cmp);
        if (p->ptrs[lindex] != l) XABORT(ABORT_NODE_POINTER_CHANGED);
        info->nodes[0] = p;

        if ((info->scxRecordsSeen[1] = (abtree_SCXRecord<DEGREE,K> *) llx_txn(tid, l, ptrsl)) == NULL) XABORT(ABORT_LLX_FAILED);
        keyindexl = l->getKeyIndex(key, cmp);
        info->nodes[1] = l;

        nkeysl = l->getKeyCount();
        found = (keyindexl < nkeysl);
        if (!found) {
            XEND();
            *result = NO_VALUE;
            *shouldRebalance = false;
            return true;
        } else {
            // replace leaf with a new copy that has the appropriate key/value pair deleted
            newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
            newNode->scxRecord = dummy;
            newNode->marked = false;

            for (int i=0;i<keyindexl;++i) {
                newNode->keys[i] = l->keys[i];
            }
            for (int i=0;i<keyindexl;++i) {
                newNode->ptrs[i] = l->ptrs[i];
            }
            for (int i=keyindexl+1;i<nkeysl;++i) {
                newNode->keys[i-1] = l->keys[i];
            }
            for (int i=keyindexl+1;i<nkeysl;++i) {
                newNode->ptrs[i-1] = l->ptrs[i];
            }
            newNode->tag = false;
            newNode->size = nkeysl-1;
            newNode->leaf = true;

            info->numberOfNodesAllocated = 1;
            info->numberOfNodesToFreeze = 2;
            info->numberOfNodes = 2;
            info->field = &p->ptrs[lindex];
            info->newNode = newNode;

            *result = l->ptrs[keyindexl];
            *shouldRebalance = (nkeysl < MIN_DEGREE);
        }

        retval = scx_txn(tid, info);
        XEND();
        reclaimMemoryAfterSCX(tid, info, true);
        if (retval) {
//            this->counters->updateChange[info->path]->inc(tid);
            return true;
        }
        return false;
    } else { 
aborthere:
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
#ifdef NO_TXNS
        cout<<"aborted erase_middle with status code "<<status;
        long long compressed = getCompressedStatus(status);
        cout<<" ="<<getAutomaticAbortNames(compressed)<<getExplicitAbortName(compressed)<<endl;
        exit(-1);
#endif
        if (info) info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        return false;
    }
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::erase_fallback(
            wrapper_info<DEGREE,K> * const info, const int tid, const K& key, bool * const shouldRebalance, void ** const result) {
    abtree_Node<DEGREE,K> * p = root;
    abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
    while (!l->isLeaf()) {
        p = l;
        l = l->getChild(key, cmp);
    }
    
    void * ptrsp[DEGREE];
    if ((info->scxRecordsSeen[0] = (abtree_SCXRecord<DEGREE,K> *) llx(tid, p, ptrsp)) == NULL) return false;
    int lindex = p->getChildIndex(key, cmp);
    if (p->ptrs[lindex] != l) return false;
    info->nodes[0] = p;
    
    void * ptrsl[DEGREE];
    if ((info->scxRecordsSeen[1] = (abtree_SCXRecord<DEGREE,K> *) llx(tid, l, ptrsl)) == NULL) return false;
    int keyindexl = l->getKeyIndex(key, cmp);
    info->nodes[1] = l;
    
    int nkeysl = l->getKeyCount();
    bool found = (keyindexl < nkeysl);
    if (!found) {
        *shouldRebalance = false;
        *result = NO_VALUE;
        TRACE COUTATOMICTID("erase_fallback: key was not found"<<endl);
        return true;
    } else {
        // replace leaf with a new copy that has the appropriate key/value pair deleted
        abtree_Node<DEGREE,K> * newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
        newNode->scxRecord = dummy;
        newNode->marked = false;
        
        for (int i=0;i<keyindexl;++i) {
            newNode->keys[i] = l->keys[i];
        }
        for (int i=0;i<keyindexl;++i) {
            newNode->ptrs[i] = l->ptrs[i];
        }
        for (int i=keyindexl+1;i<nkeysl;++i) {
            newNode->keys[i-1] = l->keys[i];
        }
        for (int i=keyindexl+1;i<nkeysl;++i) {
            newNode->ptrs[i-1] = l->ptrs[i];
        }
        newNode->tag = false;
        newNode->size = nkeysl-1;
        newNode->leaf = true;
        
        info->numberOfNodesAllocated = 1;
        info->numberOfNodesToFreeze = 2;
        info->numberOfNodes = 2;
        info->field = &p->ptrs[lindex];
        info->newNode = newNode;

        *result = l->ptrs[keyindexl];
        *shouldRebalance = (nkeysl < MIN_DEGREE);
    }
    
    bool retval = scx(tid, info);
    reclaimMemoryAfterSCX(tid, info, false);
    if (retval) {
//        this->counters->updateChange[info->path]->inc(tid);
        TRACE COUTATOMICTID("erase_fallback: key was found and erased (by replacing the leaf)"<<endl);
        return true;
    }
    TRACE COUTATOMICTID("erase_fallback: key was found, but SCX FAILED."<<endl);
    return false;
}





template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::rebalance_fast(
            wrapper_info<DEGREE,K> * const info, const int tid, const K& key, bool * const shouldRebalance) {
    abtree_Node<DEGREE,K> * p0 = GET_ALLOCATED_NODE_PTR(tid, 0);
    abtree_Node<DEGREE,K> * p1 = GET_ALLOCATED_NODE_PTR(tid, 1);
    abtree_Node<DEGREE,K> * p2 = GET_ALLOCATED_NODE_PTR(tid, 2);
    p0->marked = false;
    p1->marked = false;
    p2->marked = false;

    // try to push into the next stack PAGE before starting the txn if we might encounter a page boundary
    char stackptr = 0;
    int rem = pagesize - (((long long) &stackptr) & pagesize);
    const int estNeeded = 2000; // estimate on the space needed for stack frames of all callees beyond this point
    if (rem < estNeeded) {
        volatile char x[rem+8];
        x[rem+7] = 0; // force page to load
    }
    
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        if (numFallback > 0) { XABORT(ABORT_PROCESS_ON_FALLBACK); }
        abtree_Node<DEGREE,K> * gp = root;
        abtree_Node<DEGREE,K> * p = root;
        abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) root->ptrs[0];

        // Unrolled special case for root:
        // Note: l is NOT tagged, but it might have only 1 child pointer, which would be a problem
        if (l->isLeaf()) {
            *shouldRebalance = false; // no violations to fix
            XEND();
            return true; // nothing can be wrong with the root, if it is a leaf
        }
        bool scxRecordReady = false;
        if (l->getABDegree() == 1) { // root is internal and has only one child
            if (!rootJoinParent_fast(info, tid, p, l, p->getChildIndex(key, cmp))) XABORT(ABORT_UPDATE_FAILED);
            return true;
        }

        // root is internal, and there is nothing wrong with it, so move on
        gp = p;
        p = l;
        l = l->getChild(key, cmp);

        // check each subsequent node for tag violations and degree violations
        while (!(l->isLeaf() || l->tag || (l->getABDegree() < MIN_DEGREE && !isSentinel(l)))) {
            gp = p;
            p = l;
            l = l->getChild(key, cmp);
            TRACE COUTATOMICTID("rebalancing search visits node@"<<(long long)(void *)l<<endl);
        }

        if (!(l->tag || (l->getABDegree() < MIN_DEGREE && !isSentinel(l)))) {
            *shouldRebalance = false; // no violations to fix
            XEND();
            return true;
        }

        // tag operations take precedence
        if (l->tag) {
            if (p->getABDegree() + l->getABDegree() <= DEGREE+1) {
                if (!tagJoinParent_fast(info, tid, gp, p, gp->getChildIndex(key, cmp), l, p->getChildIndex(key, cmp))) XABORT(ABORT_UPDATE_FAILED);
                return true;
            } else {
                if (!tagSplit_fast(info, tid, gp, p, gp->getChildIndex(key, cmp), l, p->getChildIndex(key, cmp))) XABORT(ABORT_UPDATE_FAILED);
                return true;
            }
        } else { // assert (l->getABDegree() < MIN_DEGREE)
            // get sibling of l
            abtree_Node<DEGREE,K> * s;
            int lindex = p->getChildIndex(key, cmp);
            //if (p->ptrs[lindex] != l) return false;
            int sindex = lindex ? lindex-1 : lindex+1;
            s = (abtree_Node<DEGREE,K> *) p->ptrs[sindex];

            // tag operations take precedence
            if (s->tag) {
                if (p->getABDegree() + s->getABDegree() <= DEGREE+1) {
                    if (!tagJoinParent_fast(info, tid, gp, p, gp->getChildIndex(key, cmp), s, sindex)) XABORT(ABORT_UPDATE_FAILED);
                    return true;
                } else {
                    if (!tagSplit_fast(info, tid, gp, p, gp->getChildIndex(key, cmp), s, sindex)) XABORT(ABORT_UPDATE_FAILED);
                    return true;
                }
            } else {
                // either join l and s, or redistribute keys between them
                if (l->getABDegree() + s->getABDegree() < 2*MIN_DEGREE) {
                    if (!joinSibling_fast(info, tid, gp, p, gp->getChildIndex(key, cmp), l, lindex, s, sindex)) XABORT(ABORT_UPDATE_FAILED);
                    return true;
                } else {
                    if (!redistributeSibling_fast(info, tid, gp, p, gp->getChildIndex(key, cmp), l, lindex, s, sindex)) XABORT(ABORT_UPDATE_FAILED);
                    return true;
                }
            }
        }
        // impossible to get here
        if (XTEST()) XEND();
        cout<<"IMPOSSIBLE"<<endl;
        exit(-1);
    } else {
aborthere:
        // abort
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
        if (info) info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        return false;
    }
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::rebalance_middle(
            wrapper_info<DEGREE,K> * const info, const int tid, const K& key, bool * const shouldRebalance) {
    abtree_Node<DEGREE,K> * p0 = GET_ALLOCATED_NODE_PTR(tid, 0);
    abtree_Node<DEGREE,K> * p1 = GET_ALLOCATED_NODE_PTR(tid, 1);
    abtree_Node<DEGREE,K> * p2 = GET_ALLOCATED_NODE_PTR(tid, 2);
    p0->marked = false;
    p1->marked = false;
    p2->marked = false;
    
    // try to push into the next stack PAGE before starting the txn if we might encounter a page boundary
    char stackptr = 0;
    int rem = pagesize - (((long long) &stackptr) & pagesize);
    const int estNeeded = 4000; // estimate on the space needed for stack frames of all callees beyond this point
    if (rem < estNeeded) {
        volatile char x[rem+8];
        x[rem+7] = 0; // force page to load
    }
    
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        //if (numFallback > 0) { XABORT(ABORT_PROCESS_ON_FALLBACK); }

        abtree_Node<DEGREE,K> * gp = root;
        abtree_Node<DEGREE,K> * p = root;
        abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) root->ptrs[0];

        // Unrolled special case for root:
        // Note: l is NOT tagged, but it might have only 1 child pointer, which would be a problem
        if (l->isLeaf()) {
            XEND();
            *shouldRebalance = false; // no violations to fix
            return true; // nothing can be wrong with the root, if it is a leaf
        }
        bool scxRecordReady = false;
        if (l->getABDegree() == 1) { // root is internal and has only one child
            scxRecordReady = rootJoinParent_fallback<true>(info, tid, p, l, p->getChildIndex(key, cmp));
            goto doscx;
        }

        // root is internal, and there is nothing wrong with it, so move on
        gp = p;
        p = l;
        l = l->getChild(key, cmp);

        // check each subsequent node for tag violations and degree violations
        while (!(l->isLeaf() || l->tag || (l->getABDegree() < MIN_DEGREE && !isSentinel(l)))) {
            gp = p;
            p = l;
            l = l->getChild(key, cmp);
        }

        if (!(l->tag || (l->getABDegree() < MIN_DEGREE && !isSentinel(l)))) {
            XEND();
            *shouldRebalance = false; // no violations to fix
            return true;
        }

        // tag operations take precedence
        if (l->tag) {
            if (p->getABDegree() + l->getABDegree() <= DEGREE+1) {
                scxRecordReady = tagJoinParent_fallback<true>(info, tid, gp, p, gp->getChildIndex(key, cmp), l, p->getChildIndex(key, cmp));
            } else {
                scxRecordReady = tagSplit_fallback<true>(info, tid, gp, p, gp->getChildIndex(key, cmp), l, p->getChildIndex(key, cmp));
            }
        } else { // assert (l->getABDegree() < MIN_DEGREE)
            // get sibling of l
            abtree_Node<DEGREE,K> * s;
            int lindex = p->getChildIndex(key, cmp);
            //if (p->ptrs[lindex] != l) return false;
            int sindex = lindex ? lindex-1 : lindex+1;
            s = (abtree_Node<DEGREE,K> *) p->ptrs[sindex];

            // tag operations take precedence
            if (s->tag) {
                if (p->getABDegree() + s->getABDegree() <= DEGREE+1) {
                    scxRecordReady = tagJoinParent_fallback<true>(info, tid, gp, p, gp->getChildIndex(key, cmp), s, sindex);
                } else {
                    scxRecordReady = tagSplit_fallback<true>(info, tid, gp, p, gp->getChildIndex(key, cmp), s, sindex);
                }
            } else {
                // either join l and s, or redistribute keys between them
                if (l->getABDegree() + s->getABDegree() < 2*MIN_DEGREE) {
                    scxRecordReady = joinSibling_fallback<true>(info, tid, gp, p, gp->getChildIndex(key, cmp), l, lindex, s, sindex);
                } else {
                    scxRecordReady = redistributeSibling_fallback<true>(info, tid, gp, p, gp->getChildIndex(key, cmp), l, lindex, s, sindex);
                }
            }
        }
    doscx:
        // perform rebalancing step
        if (scxRecordReady) {
            bool retval = scx_txn(tid, info);
            XEND();
            reclaimMemoryAfterSCX(tid, info, true);
            if (retval) {
    //            this->counters->rebalancingSuccess[info->path]->inc(tid);
                return true;
            } else {
    //            this->counters->rebalancingFail[info->path]->inc(tid);
                return false;
            }
        } else {
            XEND();
            return false; // continue fixing violations
        }
    } else {
aborthere:
        // abort
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        if (info) info->lastAbort = status;
        return false;
    }
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::rebalance_fallback(
            wrapper_info<DEGREE,K> * const info, const int tid, const K& key, bool * const shouldRebalance) {
    abtree_Node<DEGREE,K> * gp = root;
    abtree_Node<DEGREE,K> * p = root;
    abtree_Node<DEGREE,K> * l = (abtree_Node<DEGREE,K> *) root->ptrs[0];
    
    // Unrolled special case for root:
    // Note: l is NOT tagged, but it might have only 1 child pointer, which would be a problem
    if (l->isLeaf()) {
        *shouldRebalance = false; // no violations to fix
        return true; // nothing can be wrong with the root, if it is a leaf
    }
    bool scxRecordReady = false;
    if (l->getABDegree() == 1) { // root is internal and has only one child
        scxRecordReady = rootJoinParent_fallback<false>(info, tid, p, l, p->getChildIndex(key, cmp));
        goto doscx;
    }
    
    // root is internal, and there is nothing wrong with it, so move on
    gp = p;
    p = l;
    l = l->getChild(key, cmp);
    
    // check each subsequent node for tag violations and degree violations
    while (!(l->isLeaf() || l->tag || (l->getABDegree() < MIN_DEGREE && !isSentinel(l)))) {
        gp = p;
        p = l;
        l = l->getChild(key, cmp);
        TRACE COUTATOMICTID("rebalancing search visits node@"<<(long long)(void *)l<<endl);
    }
    
    if (!(l->tag || (l->getABDegree() < MIN_DEGREE && !isSentinel(l)))) {
        *shouldRebalance = false; // no violations to fix
        return true;
    }
    
    // tag operations take precedence
    if (l->tag) {
        if (p->getABDegree() + l->getABDegree() <= DEGREE+1) {
            scxRecordReady = tagJoinParent_fallback<false>(info, tid, gp, p, gp->getChildIndex(key, cmp), l, p->getChildIndex(key, cmp));
        } else {
            scxRecordReady = tagSplit_fallback<false>(info, tid, gp, p, gp->getChildIndex(key, cmp), l, p->getChildIndex(key, cmp));
        }
    } else { // assert (l->getABDegree() < MIN_DEGREE)
        // get sibling of l
        abtree_Node<DEGREE,K> * s;
        int lindex = p->getChildIndex(key, cmp);
        //if (p->ptrs[lindex] != l) return false;
        int sindex = lindex ? lindex-1 : lindex+1;
        s = (abtree_Node<DEGREE,K> *) p->ptrs[sindex];

        // tag operations take precedence
        if (s->tag) {
            if (p->getABDegree() + s->getABDegree() <= DEGREE+1) {
                scxRecordReady = tagJoinParent_fallback<false>(info, tid, gp, p, gp->getChildIndex(key, cmp), s, sindex);
            } else {
                scxRecordReady = tagSplit_fallback<false>(info, tid, gp, p, gp->getChildIndex(key, cmp), s, sindex);
            }
        } else {
            // either join l and s, or redistribute keys between them
            if (l->getABDegree() + s->getABDegree() < 2*MIN_DEGREE) {
                scxRecordReady = joinSibling_fallback<false>(info, tid, gp, p, gp->getChildIndex(key, cmp), l, lindex, s, sindex);
            } else {
                scxRecordReady = redistributeSibling_fallback<false>(info, tid, gp, p, gp->getChildIndex(key, cmp), l, lindex, s, sindex);
            }
        }
    }
doscx:
    // perform rebalancing step
    if (scxRecordReady) {
        const int init_state = abtree_SCXRecord<DEGREE,K>::STATE_INPROGRESS;
        assert(info->allFrozen == false);
        assert(info->state == init_state);
        assert(info->numberOfNodesAllocated >= 1);
        assert(info->lastAbort == 0);
        
        TRACE { cout<<"before rebalancing step: tree = "; this->debugPrint(); cout<<endl; }
        TRACE { if (!this->validate(0, false)) exit(-1); }
        bool retval = scx(tid, info);
        reclaimMemoryAfterSCX(tid, info, false);
        if (retval) {
            TRACE { cout<<"rebalancing step successful: tree = "; this->debugPrint(); cout<<endl; }
//            this->counters->rebalancingSuccess[info->path]->inc(tid);
            TRACE { if (!this->validate(0, false)) exit(-1); }
            return true;
        } else {
//            this->counters->rebalancingFail[info->path]->inc(tid);
        }
    }
    return false; // continue fixing violations
}







template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::rootJoinParent_fast(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const p, abtree_Node<DEGREE,K> * const l, const int lindex) {
//    abtree_Node<DEGREE,K> * c = (abtree_Node<DEGREE,K> *) l->ptrs[0];
//    if (c->tag) c->tag = false;
//    p->ptrs[lindex] = c;
//    
//    XEND();
//    recordmgr->retire(tid, l);
//    return true;
    
    abtree_Node<DEGREE,K> * c = (abtree_Node<DEGREE,K> *) l->ptrs[0];
    abtree_Node<DEGREE,K> * newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
    newNode->marked = false;
    newNode->scxRecord = dummy;

    for (int i=0;i<c->getKeyCount();++i) {
        newNode->keys[i] = c->keys[i];
    }
    for (int i=0;i<c->getABDegree();++i) {
        newNode->ptrs[i] = c->ptrs[i];
    }

    newNode->tag = false; // since p is root(holder), newNode is the new actual root, so its tag is false
    newNode->size = c->size;
    newNode->leaf = c->leaf;

    p->ptrs[lindex] = newNode;
    
    XEND();
    REPLACE_ALLOCATED_NODE(tid, 0);
    recordmgr->retire(tid, c);
    recordmgr->retire(tid, l);
    return true;
}
//    // perform rebalancing step
//    const int init_state = abtree_SCXRecord<DEGREE,K>::STATE_INPROGRESS;
//    bool retval = scx(tid, info);
//    if (retval) {
//        XEND();
//        this->counters->rebalancingSuccess[info->path]->inc(tid);
//        return true;
//    } else {
//        XEND();
//        this->counters->rebalancingFail[info->path]->inc(tid);
//        return false;
//    }

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::tagJoinParent_fast(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex) {
    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->marked = false;
    newp->scxRecord = dummy;

    // elements of p left of l
    int k1=0, k2=0;
    for (int i=0;i<lindex;++i) {
        newp->keys[k1++] = p->keys[i];
    }
    for (int i=0;i<lindex;++i) {
        newp->ptrs[k2++] = p->ptrs[i];
    }

    // contents of l
    for (int i=0;i<l->getKeyCount();++i) {
        newp->keys[k1++] = l->keys[i];
    }
    for (int i=0;i<l->getABDegree();++i) {
        newp->ptrs[k2++] = l->ptrs[i];
    }

    // remaining elements of p
    for (int i=lindex;i<p->getKeyCount();++i) {
        newp->keys[k1++] = p->keys[i];
    }
    // skip child pointer for lindex
    for (int i=lindex+1;i<p->getABDegree();++i) {
        newp->ptrs[k2++] = p->ptrs[i];
    }
    
    newp->tag = false;
    newp->size = p->size + l->size - 1;
    newp->leaf = false;

    gp->ptrs[pindex] = newp;

    XEND();
    REPLACE_ALLOCATED_NODE(tid, 0);
    recordmgr->retire(tid, l);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::tagSplit_fast(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex) {
    // create new nodes for update
    const int sz = p->getABDegree() + l->getABDegree() - 1;
    const int leftsz = sz/2;
    const int rightsz = sz - leftsz;
    
    K keys[2*DEGREE+1];
    void * ptrs[2*DEGREE+1];
    int k1=0, k2=0;

    // elements of p left than l
    for (int i=0;i<lindex;++i) {
        keys[k1++] = p->keys[i];
    }
    for (int i=0;i<lindex;++i) {
        ptrs[k2++] = p->ptrs[i];
    }

    // contents of l
    for (int i=0;i<l->getKeyCount();++i) {
        keys[k1++] = l->keys[i];
    }
    for (int i=0;i<l->getABDegree();++i) {
        ptrs[k2++] = l->ptrs[i];
    }

    // remaining elements of p
    for (int i=lindex;i<p->getKeyCount();++i) {
        keys[k1++] = p->keys[i];
    }
    // skip child pointer for lindex
    for (int i=lindex+1;i<p->getABDegree();++i) {
        ptrs[k2++] = p->ptrs[i];
    }
    
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newleft = GET_ALLOCATED_NODE_PTR(tid, 1);
    newleft->scxRecord = dummy;
    newleft->marked = false;

    abtree_Node<DEGREE,K> * newright = GET_ALLOCATED_NODE_PTR(tid, 2);
    newright->scxRecord = dummy;
    newright->marked = false;

    k1=0;
    k2=0;
    
    for (int i=0;i<leftsz-1;++i) {
        newleft->keys[i] = keys[k1++];
    }
    for (int i=0;i<leftsz;++i) {
        newleft->ptrs[i] = ptrs[k2++];
    }
    newleft->tag = false;
    newleft->size = leftsz;
    newleft->leaf = false;
    
    newp->keys[0] = keys[k1++];
    newp->ptrs[0] = newleft;
    newp->ptrs[1] = newright;
    newp->tag = (gp != root);
    newp->size = 2;
    newp->leaf = false;
    
    for (int i=0;i<rightsz-1;++i) {
        newright->keys[i] = keys[k1++];
    }
    for (int i=0;i<rightsz;++i) {
        newright->ptrs[i] = ptrs[k2++];
    }
    newright->tag = false;
    newright->size = rightsz;
    newright->leaf = false;
    
    gp->ptrs[pindex] = newp;
    
    XEND();
    recordmgr->retire(tid, l);
    recordmgr->retire(tid, p);
    REPLACE_ALLOCATED_NODE(tid, 0);
    REPLACE_ALLOCATED_NODE(tid, 1);
    REPLACE_ALLOCATED_NODE(tid, 2);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::joinSibling_fast(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex) {
    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newl = GET_ALLOCATED_NODE_PTR(tid, 1);
    newl->scxRecord = dummy;
    newl->marked = false;

    // create newl by joining s to l

    abtree_Node<DEGREE,K> * left;
    abtree_Node<DEGREE,K> * right;
    int leftindex;
    int rightindex;
    if (lindex < sindex) {
        left = l;
        leftindex = lindex;
        right = s;
        rightindex = sindex;
    } else {
        left = s;
        leftindex = sindex;
        right = l;
        rightindex = lindex;
    }
    
    int k1=0, k2=0;
    for (int i=0;i<left->getKeyCount();++i) {
        newl->keys[k1++] = left->keys[i];
    }
    for (int i=0;i<left->getABDegree();++i) {
        newl->ptrs[k2++] = left->ptrs[i];
    }
    if (!left->isLeaf()) newl->keys[k1++] = p->keys[leftindex];
    for (int i=0;i<right->getKeyCount();++i) {
        newl->keys[k1++] = right->keys[i];
    }
    for (int i=0;i<right->getABDegree();++i) {
        newl->ptrs[k2++] = right->ptrs[i];
    }
    
    // create newp from p by:
    // 1. skipping the key for leftindex and child pointer for sindex
    // 2. replacing l with newl
    for (int i=0;i<leftindex;++i) {
        newp->keys[i] = p->keys[i];
    }
    for (int i=0;i<sindex;++i) {
        newp->ptrs[i] = p->ptrs[i];
    }
    for (int i=leftindex+1;i<p->getKeyCount();++i) {
        newp->keys[i-1] = p->keys[i];
    }
    for (int i=sindex+1;i<p->getABDegree();++i) {
        newp->ptrs[i-1] = p->ptrs[i];
    }
    // replace l with newl
    newp->ptrs[lindex - (lindex > sindex)] = newl;
    
    newp->tag = false;
    newp->size = p->size - 1;
    newp->leaf = false;
    newl->tag = false;
    newl->size = l->size + s->size;
    newl->leaf = l->leaf;
    
    gp->ptrs[pindex] = newp;    
    
    XEND();
    REPLACE_ALLOCATED_NODE(tid, 0);
    REPLACE_ALLOCATED_NODE(tid, 1);
    recordmgr->retire(tid, p);
    recordmgr->retire(tid, l);
    recordmgr->retire(tid, s);
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::redistributeSibling_fast(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex) {
    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newl = GET_ALLOCATED_NODE_PTR(tid, 1);
    newl->scxRecord = dummy;
    newl->marked = false;

    abtree_Node<DEGREE,K> * news = GET_ALLOCATED_NODE_PTR(tid, 2);
    news->scxRecord = dummy;
    news->marked = false;

    // create newl and news by evenly sharing the keys + pointers of l and s
    int sz = l->getABDegree() + s->getABDegree();
    int leftsz = sz/2;
    int rightsz = sz-leftsz;
    kvpair<K> tosort[2*DEGREE+1];
    
    abtree_Node<DEGREE,K> * left;
    abtree_Node<DEGREE,K> * right;
    abtree_Node<DEGREE,K> * newleft;
    abtree_Node<DEGREE,K> * newright;
    int leftindex;
    int rightindex;
    if (lindex < sindex) {
        left = l;
        newleft = newl;
        leftindex = lindex;
        right = s;
        newright = news;
        rightindex = sindex;
    } else {
        left = s;
        newleft = news;
        leftindex = sindex;
        right = l;
        newright = newl;
        rightindex = lindex;
    }
    assert(rightindex == 1+leftindex);
    
    // combine the contents of l and s (and one key from p)
    int k1=0, k2=0;
    for (int i=0;i<left->getKeyCount();++i) {
        tosort[k1++].key = left->keys[i];
    }
    for (int i=0;i<left->getABDegree();++i) {
        tosort[k2++].val = left->ptrs[i];
    }
    if (!left->isLeaf()) tosort[k1++].key = p->keys[leftindex];
    for (int i=0;i<right->getKeyCount();++i) {
        tosort[k1++].key = right->keys[i];
    }
    for (int i=0;i<right->getABDegree();++i) {
        tosort[k2++].val = right->ptrs[i];
    }
    //assert(k1 == sz+left->isLeaf()); // only holds in general if something like opacity is satisfied
    assert(!gp->tag);
    assert(!p->tag);
    assert(!left->tag);
    assert(!right->tag);
    assert(k1 <= sz+1);
    assert(k2 == sz);
    assert(!left->isLeaf() || k1 == k2);
    
    // sort if this is a leaf
    if (left->isLeaf()) qsort(tosort, k1, sizeof(kvpair<K>), kv_compare<K,Compare>);
    
    // distribute contents between newleft and newright
    k1=0;
    k2=0;
    for (int i=0;i<leftsz - !left->isLeaf();++i) {
        newleft->keys[i] = tosort[k1++].key;
    }
    for (int i=0;i<leftsz;++i) {
        newleft->ptrs[i] = tosort[k2++].val;
    }
    // reserve one key for the parent (to go between newleft and newright))
    K keyp = tosort[k1].key;
    if (!left->isLeaf()) ++k1;
    for (int i=0;i<rightsz - !left->isLeaf();++i) {
        newright->keys[i] = tosort[k1++].key;
    }
    for (int i=0;i<rightsz;++i) {
        newright->ptrs[i] = tosort[k2++].val;
    }
    
    // create newp from p by replacing left with newleft and right with newright,
    // and replacing one key (between these two pointers)
    for (int i=0;i<p->getKeyCount();++i) {
        newp->keys[i] = p->keys[i];
    }
    for (int i=0;i<p->getABDegree();++i) {
        newp->ptrs[i] = p->ptrs[i];
    }
    newp->keys[leftindex] = keyp;
    newp->ptrs[leftindex] = newleft;
    newp->ptrs[rightindex] = newright;
    newp->tag = false;
    newp->size = p->size;
    newp->leaf = false;
    
    newleft->tag = false;
    newleft->size = leftsz;
    newleft->leaf = left->leaf;
    newright->tag = false;
    newright->size = rightsz;
    newright->leaf = right->leaf;

    gp->ptrs[pindex] = newp;
    
    XEND();
    REPLACE_ALLOCATED_NODE(tid, 0);
    REPLACE_ALLOCATED_NODE(tid, 1);
    REPLACE_ALLOCATED_NODE(tid, 2);
    recordmgr->retire(tid, l);
    recordmgr->retire(tid, s);
    recordmgr->retire(tid, p);
    return true;
}







// np++regex a: \) llx\(([^\)]+)\)
// np++regex b: \) \(in_txn ? llx_txn\(\1\) : llx\(\1\)\)

template<int DEGREE, typename K, class Compare, class RecManager> template<bool in_txn>
bool abtree<DEGREE,K,Compare,RecManager>::rootJoinParent_fallback(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const p, abtree_Node<DEGREE,K> * const l, const int lindex) {
    TRACE COUTATOMICTID("rootJoinParent_fallback"<<endl);
    // perform LLX on p and l, and l's only child c
    void * ptrsp[DEGREE];
    if ((info->scxRecordsSeen[0] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, p, ptrsp) : llx(tid, p, ptrsp))) == NULL) return false;
    if (p->ptrs[lindex] != l) return false;
    info->nodes[0] = p;

    void * ptrsl[DEGREE];
    if ((info->scxRecordsSeen[1] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, l, ptrsl) : llx(tid, l, ptrsl))) == NULL) return false;
    info->nodes[1] = l;
    
    void * ptrsc[DEGREE];
    abtree_Node<DEGREE,K> * c = (abtree_Node<DEGREE,K> *) l->ptrs[0];
    if ((info->scxRecordsSeen[2] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, c, ptrsc) : llx(tid, c, ptrsc))) == NULL) return false;
    info->nodes[2] = c;
    
    // prepare SCX record for update
    abtree_Node<DEGREE,K> * newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
    newNode->marked = false;
    newNode->scxRecord = dummy;

    for (int i=0;i<c->getKeyCount();++i) {
        newNode->keys[i] = c->keys[i];
    }
    for (int i=0;i<c->getABDegree();++i) {
        newNode->ptrs[i] = c->ptrs[i];
    }

    assert(!p->tag);
    assert(!l->tag);
    assert(p == root);
    newNode->tag = false; // since p is root(holder), newNode is the new actual root, so its tag is false
    newNode->size = c->size;
    newNode->leaf = c->leaf;

    info->numberOfNodesAllocated = 1;
    info->numberOfNodesToFreeze = 3;
    info->numberOfNodes = 3;
    info->field = &p->ptrs[lindex];
    info->newNode = newNode;
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager> template<bool in_txn>
bool abtree<DEGREE,K,Compare,RecManager>::tagJoinParent_fallback(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex) {
    TRACE COUTATOMICTID("tagJoinParent_fallback"<<endl);
    // perform LLX on gp, p and l
    void * ptrsgp[DEGREE];
    if ((info->scxRecordsSeen[0] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, gp, ptrsgp) : llx(tid, gp, ptrsgp))) == NULL) return false;
    if (gp->ptrs[pindex] != p) return false;
    info->nodes[0] = gp;

    void * ptrsp[DEGREE];
    if ((info->scxRecordsSeen[1] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, p, ptrsp) : llx(tid, p, ptrsp))) == NULL) return false;
    if (p->ptrs[lindex] != l) return false;
    info->nodes[1] = p;

    void * ptrsl[DEGREE];
    if ((info->scxRecordsSeen[2] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, l, ptrsl) : llx(tid, l, ptrsl))) == NULL) return false;
    info->nodes[2] = l;
    
    // create new nodes for update
    abtree_Node<DEGREE,K> * newNode = GET_ALLOCATED_NODE_PTR(tid, 0);
    newNode->marked = false;
    newNode->scxRecord = dummy;

    // elements of p left of l
    int k1=0, k2=0;
    for (int i=0;i<lindex;++i) {
        newNode->keys[k1++] = p->keys[i];
    }
    for (int i=0;i<lindex;++i) {
        newNode->ptrs[k2++] = p->ptrs[i];
    }

    // contents of l
    for (int i=0;i<l->getKeyCount();++i) {
        newNode->keys[k1++] = l->keys[i];
    }
    for (int i=0;i<l->getABDegree();++i) {
        newNode->ptrs[k2++] = l->ptrs[i];
    }

    // remaining elements of p
    for (int i=lindex;i<p->getKeyCount();++i) {
        newNode->keys[k1++] = p->keys[i];
    }
    // skip child pointer for lindex
    for (int i=lindex+1;i<p->getABDegree();++i) {
        newNode->ptrs[k2++] = p->ptrs[i];
    }
    
    newNode->tag = false;
    newNode->size = p->size + l->size - 1;
    newNode->leaf = false;
    assert(!gp->tag);
    assert(!p->tag);
    assert(l->tag);
    assert(k2 == newNode->size);
    assert(k1 == newNode->size - !newNode->isLeaf());

    // prepare SCX record for update
    info->numberOfNodesAllocated = 1;
    info->numberOfNodesToFreeze = 3;
    info->numberOfNodes = 3;
    info->field = &gp->ptrs[pindex];
    info->newNode = newNode;
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager> template<bool in_txn>
bool abtree<DEGREE,K,Compare,RecManager>::tagSplit_fallback(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex) {
    TRACE COUTATOMICTID("tagSplit_fallback"<<endl);
    // perform LLX on gp, p and l
    void * ptrsgp[DEGREE];
    if ((info->scxRecordsSeen[0] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, gp, ptrsgp) : llx(tid, gp, ptrsgp))) == NULL) return false;
    if (gp->ptrs[pindex] != p) return false;
    info->nodes[0] = gp;

    void * ptrsp[DEGREE];
    if ((info->scxRecordsSeen[1] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, p, ptrsp) : llx(tid, p, ptrsp))) == NULL) return false;
    if (p->ptrs[lindex] != l) return false;
    info->nodes[1] = p;

    void * ptrsl[DEGREE];
    if ((info->scxRecordsSeen[2] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, l, ptrsl) : llx(tid, l, ptrsl))) == NULL) return false;
    info->nodes[2] = l;
    
    // create new nodes for update
    const int sz = p->getABDegree() + l->getABDegree() - 1;
    const int leftsz = sz/2;
    const int rightsz = sz - leftsz;
    
    K keys[sz-1];
    void * ptrs[sz];
    int k1=0, k2=0;

    // elements of p left than l
    for (int i=0;i<lindex;++i) {
        keys[k1++] = p->keys[i];
    }
    for (int i=0;i<lindex;++i) {
        ptrs[k2++] = p->ptrs[i];
    }

    // contents of l
    for (int i=0;i<l->getKeyCount();++i) {
        keys[k1++] = l->keys[i];
    }
    for (int i=0;i<l->getABDegree();++i) {
        ptrs[k2++] = l->ptrs[i];
    }

    // remaining elements of p
    for (int i=lindex;i<p->getKeyCount();++i) {
        keys[k1++] = p->keys[i];
    }
    // skip child pointer for lindex
    for (int i=lindex+1;i<p->getABDegree();++i) {
        ptrs[k2++] = p->ptrs[i];
    }
    assert(!gp->tag);
    assert(!p->tag);
    assert(l->tag);
    assert(k1 <= sz-1);
    assert(k2 <= sz);
    
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newleft = GET_ALLOCATED_NODE_PTR(tid, 1);
    newleft->scxRecord = dummy;
    newleft->marked = false;

    abtree_Node<DEGREE,K> * newright = GET_ALLOCATED_NODE_PTR(tid, 2);
    newright->scxRecord = dummy;
    newright->marked = false;

    k1=0;
    k2=0;
    
    for (int i=0;i<leftsz-1;++i) {
        newleft->keys[i] = keys[k1++];
    }
    for (int i=0;i<leftsz;++i) {
        newleft->ptrs[i] = ptrs[k2++];
    }
    newleft->tag = false;
    newleft->size = leftsz;
    newleft->leaf = false;
    
    newp->keys[0] = keys[k1++];
    newp->ptrs[0] = newleft;
    newp->ptrs[1] = newright;
    newp->tag = (gp != root);
    newp->size = 2;
    newp->leaf = false;
    
    for (int i=0;i<rightsz-1;++i) {
        newright->keys[i] = keys[k1++];
    }
    for (int i=0;i<rightsz;++i) {
        newright->ptrs[i] = ptrs[k2++];
    }
    newright->tag = false;
    newright->size = rightsz;
    newright->leaf = false;
    
    // prepare SCX record for update
    info->numberOfNodesAllocated = 3;
    info->numberOfNodesToFreeze = 3;
    info->numberOfNodes = 3;
    info->field = &gp->ptrs[pindex];
    info->newNode = newp;
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager> template<bool in_txn>
bool abtree<DEGREE,K,Compare,RecManager>::joinSibling_fallback(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex) {
    TRACE COUTATOMICTID("joinSibling_fallback"<<endl);
    // perform LLX on gp, p and l
    void * ptrsgp[DEGREE];
    if ((info->scxRecordsSeen[0] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, gp, ptrsgp) : llx(tid, gp, ptrsgp))) == NULL) return false;
    if (gp->ptrs[pindex] != p) return false;
    info->nodes[0] = gp;
    
    void * ptrsp[DEGREE];
    if ((info->scxRecordsSeen[1] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, p, ptrsp) : llx(tid, p, ptrsp))) == NULL) return false;
    if (p->ptrs[lindex] != l) return false;
    if (p->ptrs[sindex] != s) return false;
    info->nodes[1] = p;

    int freezeorderl = (sindex > lindex) ? 2 : 3;
    int freezeorders = (sindex > lindex) ? 3 : 2;
    
    void * ptrsl[DEGREE];
    if ((info->scxRecordsSeen[freezeorderl] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, l, ptrsl) : llx(tid, l, ptrsl))) == NULL) return false;
    info->nodes[freezeorderl] = l;

    void * ptrss[DEGREE];
    if ((info->scxRecordsSeen[freezeorders] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, s, ptrss) : llx(tid, s, ptrss))) == NULL) return false;
    info->nodes[freezeorders] = s;
    
    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newl = GET_ALLOCATED_NODE_PTR(tid, 1);
    newl->scxRecord = dummy;
    newl->marked = false;

    // create newl by joining s to l

    abtree_Node<DEGREE,K> * left;
    abtree_Node<DEGREE,K> * right;
    int leftindex;
    int rightindex;
    if (lindex < sindex) {
        left = l;
        leftindex = lindex;
        right = s;
        rightindex = sindex;
    } else {
        left = s;
        leftindex = sindex;
        right = l;
        rightindex = lindex;
    }
    
    int k1=0, k2=0;
    for (int i=0;i<left->getKeyCount();++i) {
        newl->keys[k1++] = left->keys[i];
    }
    for (int i=0;i<left->getABDegree();++i) {
        newl->ptrs[k2++] = left->ptrs[i];
    }
    if (!left->isLeaf()) newl->keys[k1++] = p->keys[leftindex];
    for (int i=0;i<right->getKeyCount();++i) {
        newl->keys[k1++] = right->keys[i];
    }
    for (int i=0;i<right->getABDegree();++i) {
        newl->ptrs[k2++] = right->ptrs[i];
    }
    
    // create newp from p by:
    // 1. skipping the key for leftindex and child pointer for sindex
    // 2. replacing l with newl
    for (int i=0;i<leftindex;++i) {
        newp->keys[i] = p->keys[i];
    }
    for (int i=0;i<sindex;++i) {
        newp->ptrs[i] = p->ptrs[i];
    }
    for (int i=leftindex+1;i<p->getKeyCount();++i) {
        newp->keys[i-1] = p->keys[i];
    }
    for (int i=sindex+1;i<p->getABDegree();++i) {
        newp->ptrs[i-1] = p->ptrs[i];
    }
    // replace l with newl
    newp->ptrs[lindex - (lindex > sindex)] = newl;
    
    newp->tag = false;
    newp->size = p->size - 1;
    newp->leaf = false;
    newl->tag = false;
    newl->size = l->size + s->size;
    newl->leaf = l->leaf;
    
    assert(!gp->tag);
    assert(!p->tag);
    assert(!left->tag);
    assert(!right->tag);
    assert(k2 == newl->size);
    if (k1 != newl->size - !newl->isLeaf()) {
//        cout<<"left="; left->printNode(cout); cout<<endl;
//        cout<<"right="; right->printNode(cout); cout<<endl;
//        cout<<"newp="; newp->printNode(cout); cout<<endl;
//        cout<<"newl="; newl->printNode(cout); cout<<endl;
        cout<<"keys=";
        for (int i=0;i<k1;++i) {
            cout<<" "<<newl->keys[i];
        }
        cout<<endl;
        cout<<"k1="<<k1<<" k2="<<k2<<" newl->size="<<newl->size<<" isleaf="<<newl->isLeaf()<<endl;
        this->debugPrint();
        cout<<endl;
        exit(-1);
    }
    assert(k1 == newl->size - !newl->isLeaf());

    // prepare SCX record for update
    info->numberOfNodesAllocated = 2;
    info->numberOfNodesToFreeze = 4;
    info->numberOfNodes = 4;
    info->field = &gp->ptrs[pindex];
    info->newNode = newp;
    
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager> template<bool in_txn>
bool abtree<DEGREE,K,Compare,RecManager>::redistributeSibling_fallback(wrapper_info<DEGREE,K> * const info, const int tid, abtree_Node<DEGREE,K> * const gp, abtree_Node<DEGREE,K> * const p, const int pindex, abtree_Node<DEGREE,K> * const l, const int lindex, abtree_Node<DEGREE,K> * const s, const int sindex) {
    TRACE COUTATOMICTID("redistributeSibling_fallback"<<endl);
    // perform LLX on gp, p, l and s
    void * ptrsgp[DEGREE];
    if ((info->scxRecordsSeen[0] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, gp, ptrsgp) : llx(tid, gp, ptrsgp))) == NULL) return false;
    if (gp->ptrs[pindex] != p) return false;
    info->nodes[0] = gp;
    
    void * ptrsp[DEGREE];
    if ((info->scxRecordsSeen[1] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, p, ptrsp) : llx(tid, p, ptrsp))) == NULL) return false;
    if (p->ptrs[lindex] != l) return false;
    if (p->ptrs[sindex] != s) return false;
    info->nodes[1] = p;

    int freezeorderl = (sindex > lindex) ? 2 : 3;
    int freezeorders = (sindex > lindex) ? 3 : 2;
    
    void * ptrsl[DEGREE];
    if ((info->scxRecordsSeen[freezeorderl] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, l, ptrsl) : llx(tid, l, ptrsl))) == NULL) return false;
    info->nodes[freezeorderl] = l;

    void * ptrss[DEGREE];
    if ((info->scxRecordsSeen[freezeorders] = (abtree_SCXRecord<DEGREE,K> *) (in_txn ? llx_txn(tid, s, ptrss) : llx(tid, s, ptrss))) == NULL) return false;
    info->nodes[freezeorders] = s;

    // create new nodes for update
    abtree_Node<DEGREE,K> * newp = GET_ALLOCATED_NODE_PTR(tid, 0);
    newp->scxRecord = dummy;
    newp->marked = false;

    abtree_Node<DEGREE,K> * newl = GET_ALLOCATED_NODE_PTR(tid, 1);
    newl->scxRecord = dummy;
    newl->marked = false;

    abtree_Node<DEGREE,K> * news = GET_ALLOCATED_NODE_PTR(tid, 2);
    news->scxRecord = dummy;
    news->marked = false;

    // create newl and news by evenly sharing the keys + pointers of l and s
    int sz = l->getABDegree() + s->getABDegree();
    int leftsz = sz/2;
    int rightsz = sz-leftsz;
    kvpair<K> tosort[sz+1];
//    K keys[sz+1];
//    void * ptrs[sz];
    
    abtree_Node<DEGREE,K> * left;
    abtree_Node<DEGREE,K> * right;
    abtree_Node<DEGREE,K> * newleft;
    abtree_Node<DEGREE,K> * newright;
    int leftindex;
    int rightindex;
    if (lindex < sindex) {
        left = l;
        newleft = newl;
        leftindex = lindex;
        right = s;
        newright = news;
        rightindex = sindex;
    } else {
        left = s;
        newleft = news;
        leftindex = sindex;
        right = l;
        newright = newl;
        rightindex = lindex;
    }
    assert(rightindex == 1+leftindex);
    
    // combine the contents of l and s (and one key from p)
    int k1=0, k2=0;
    for (int i=0;i<left->getKeyCount();++i) {
//        keys[k1++] = left->keys[i];
        tosort[k1++].key = left->keys[i];
    }
    for (int i=0;i<left->getABDegree();++i) {
//        ptrs[k2++] = left->ptrs[i];
        tosort[k2++].val = left->ptrs[i];
    }
    if (!left->isLeaf()) tosort[k1++].key = p->keys[leftindex];
    for (int i=0;i<right->getKeyCount();++i) {
//        keys[k1++] = right->keys[i];
        tosort[k1++].key = right->keys[i];
    }
    for (int i=0;i<right->getABDegree();++i) {
//        ptrs[k2++] = right->ptrs[i];
        tosort[k2++].val = right->ptrs[i];
    }
    //assert(k1 == sz+left->isLeaf()); // only holds in general if something like opacity is satisfied
    assert(!gp->tag);
    assert(!p->tag);
    assert(!left->tag);
    assert(!right->tag);
    assert(k1 <= sz+1);
    assert(k2 == sz);
    assert(!left->isLeaf() || k1 == k2);
    
    // sort if this is a leaf
    if (left->isLeaf()) qsort(tosort, k1, sizeof(kvpair<K>), kv_compare<K,Compare>);
    
    // distribute contents between newleft and newright
    k1=0;
    k2=0;
    for (int i=0;i<leftsz - !left->isLeaf();++i) {
        newleft->keys[i] = tosort[k1++].key;
    }
    for (int i=0;i<leftsz;++i) {
        newleft->ptrs[i] = tosort[k2++].val;
    }
    // reserve one key for the parent (to go between newleft and newright))
    K keyp = tosort[k1].key;
    if (!left->isLeaf()) ++k1;
    for (int i=0;i<rightsz - !left->isLeaf();++i) {
        newright->keys[i] = tosort[k1++].key;
    }
    for (int i=0;i<rightsz;++i) {
        newright->ptrs[i] = tosort[k2++].val;
    }
    
    // create newp from p by replacing left with newleft and right with newright,
    // and replacing one key (between these two pointers)
    for (int i=0;i<p->getKeyCount();++i) {
        newp->keys[i] = p->keys[i];
    }
    for (int i=0;i<p->getABDegree();++i) {
        newp->ptrs[i] = p->ptrs[i];
    }
    newp->keys[leftindex] = keyp;
    newp->ptrs[leftindex] = newleft;
    newp->ptrs[rightindex] = newright;
    newp->tag = false;
    newp->size = p->size;
    newp->leaf = false;
    
    newleft->tag = false;
    newleft->size = leftsz;
    newleft->leaf = left->leaf;
    newright->tag = false;
    newright->size = rightsz;
    newright->leaf = right->leaf;

    TRACE { cout<<"left="; left->printTreeFile(cout); cout<<endl; }
    TRACE { cout<<"right="; right->printTreeFile(cout); cout<<endl; }
    TRACE { cout<<"newleft="; newleft->printTreeFile(cout); cout<<endl; }
    TRACE { cout<<"newright="; newright->printTreeFile(cout); cout<<endl; }

    // prepare SCX record for update
    info->numberOfNodesAllocated = 3;
    info->numberOfNodesToFreeze = 4;
    info->numberOfNodes = 4;
    info->field = &gp->ptrs[pindex];
    info->newNode = newp;
    
    return true;
}









template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::validate(abtree_Node<DEGREE,K> * const node, const int currdepth, const int leafdepth) {
    abtree_SCXRecord<DEGREE,K> * scx = node->scxRecord;
    int state = (IS_VERSION_NUMBER(scx) ? abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED : scx->state);
    if (state == abtree_SCXRecord<DEGREE,K>::STATE_INPROGRESS) {
        cout<<"validation error: SCX record with state in progress pointed to by reachable node"<<endl;
        debugPrint();
        return false;
    }
    if (node->marked) {
        cout<<"validation error: marked node is reachable"<<endl;
        debugPrint();
        return false;
    }

    // base case: node is a leaf
    if (node->isLeaf()) {
        if (!node->getKeyCount() >= MIN_DEGREE) {
            cout<<"validation error: key count < MIN_DEGREE"<<endl;
            debugPrint();
            return false;
        }
        if (currdepth != leafdepth) {
            cout<<"validation error: tagged-depth is not the same for all leaves ("<<currdepth<<" vs "<<leafdepth<<")"<<endl;
            debugPrint();
            return false;
        }
        return true;
    }
    
    // recursive case: node is internal
    
    if (node->getKeyCount() == 0) {
        return validate((abtree_Node<DEGREE,K> *) node->ptrs[0], (!node->tag)+currdepth, leafdepth);
    }
    
    // check keys are in order
    for (int i=1;i<node->getKeyCount();++i) {
        if (!cmp((const K&) node->keys[i-1], (const K&) node->keys[i])) {
            cout<<"validation error: keys not in order"<<endl;
            debugPrint();
            return false;
        }
    }
    
    // check keys of children are less than the parent's key just to their right
    for (int i=0;i<node->getKeyCount();++i) {
        abtree_Node<DEGREE,K> * const child = (abtree_Node<DEGREE,K> *) node->ptrs[i];
        int childnkeys = child->getKeyCount();
        for (int j=0;j<childnkeys;++j) {
            if (!cmp((const K&) child->keys[j], (const K&) node->keys[i])) {
                cout<<"validation error: keys in child["<<i<<"] not less than key["<<i<<"] of parent with keys:";
                for (int k=0;k<node->getKeyCount();++k) {
                    cout<<" "<<node->keys[j];
                }
                cout<<endl;
                debugPrint();
                return false;
            }
        }
    }
    
    assert(node->getABDegree() > 0);
    // check keys of rightmost child are greater than or equal to parent's last key
    abtree_Node<DEGREE,K> * const child = (abtree_Node<DEGREE,K> *) node->ptrs[node->getKeyCount()];
    int childnkeys = child->getKeyCount();
    for (int j=0;j<childnkeys;++j) {
        if (cmp((const K&) child->keys[j], (const K&) node->keys[node->getKeyCount()-1])) {
            cout<<"validation error: keys in child["<<node->getKeyCount()<<"] not greater than or equal to key["<<(node->getKeyCount()-1)<<"] of parent"<<endl;
            debugPrint();
            return false;
        }
    }
    
    // recurse on children
    for (int i=0;i<node->getABDegree();++i) {
        if (!validate((abtree_Node<DEGREE,K> *) node->ptrs[i], (!node->tag)+currdepth, leafdepth)) return false;
    }
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::validate(const long long keysum, const bool checkkeysum) {
    if (checkkeysum) {
        long long treekeysum = this->debugKeySum();
        if (treekeysum != keysum) {
            cout<<"validation error: keysum "<<keysum<<" != tree keysum "<<treekeysum<<endl;
            debugPrint();
            return false;
        }
    }
    if (root->getABDegree() != 1) {
        cout<<"validation error: root has degree "<<root->getABDegree()<<" != 1"<<endl;
        debugPrint();
        return false;
    }
    
    // get tagged-depth of some leaf
    abtree_Node<DEGREE,K> * curr = (abtree_Node<DEGREE,K> *) root->ptrs[0];
    int leafdepth = 0;
    while (!curr->isLeaf()) {
        curr = (abtree_Node<DEGREE,K> *) curr->ptrs[0];
        leafdepth += (!curr->tag);
    }
    return validate((abtree_Node<DEGREE,K> *) root->ptrs[0], 0, leafdepth);
}

// this internal function is called only by scx(), and only when otherSCX is protected by a call to shmem->protect
template<int DEGREE, typename K, class Compare, class RecManager>
bool abtree<DEGREE,K,Compare,RecManager>::tryRetireSCXRecord(const int tid, abtree_SCXRecord<DEGREE,K> * const otherSCX, abtree_Node<DEGREE,K> * const node) {
    if (otherSCX == dummy) return false; // never retire the dummy scx record!
    if (otherSCX->state == abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED) {
        // in this tree, committed scx records are only pointed to by one node.
        // so, when this function is called, the scx record is already retired.
        recordmgr->retire(tid, otherSCX);
        return true;
    } else { // assert: scx->state >= STATE_ABORTED
        const int state_aborted = abtree_SCXRecord<DEGREE,K>::STATE_ABORTED;
//        assert(otherSCX->state >= state_aborted); /* state is aborted */
        // node->scxRecord no longer points to scx, so we set
        // the corresponding bit in scx->state to 0.
        // when state == ABORT_STATE_NO_FLAGS(state), scx is retired.
        const int n = otherSCX->numberOfNodesToFreeze;
        abtree_Node<DEGREE,K> * volatile * const otherNodes = otherSCX->nodes;
        bool casSucceeded = false;
        int stateNew = -1;
        for (int i=0;i<n;++i) {
            if (otherNodes[i] == node) {
                while (!casSucceeded) {
//                    TRACE COUTATOMICTID("attempting state CAS..."<<endl);
                    int stateOld = otherSCX->state;
                    stateNew = STATE_GET_WITH_FLAG_OFF(stateOld, i);
//                    DEBUG assert(stateOld >= state_aborted);
//                    DEBUG assert(stateNew >= state_aborted);
//                    assert(stateNew < stateOld);
                    casSucceeded = __sync_bool_compare_and_swap(&otherSCX->state, stateOld, stateNew);
                }
                break;
            }
        }
        // many scxs can all be CASing state and trying to retire this node.
        // the one who gets to invoke retire() is the one whose CAS sets
        // the flag subfield of scx->state to 0.
        if (casSucceeded && STATE_GET_FLAGS(stateNew) == 0) {
            recordmgr->retire(tid, otherSCX);
            return true;
        }
    }
    return false;
}

// IF you are using DEBRA+, then you may call this only in a quiescent state.
// (if this is being called from crash recovery, then all nodes in nodes[] and
// the scx record "info" must be Qprotected.)
// for other schemes, you may call this in a non-quiescent state.
// the scx records in scxRecordsSeen must be protected (or we must know no one
// can have freed them out from under us, which is the case here).
template<int DEGREE, typename K, class Compare, class RecManager>
void abtree<DEGREE,K,Compare,RecManager>::reclaimMemoryAfterSCX(
            const int tid,
            wrapper_info<DEGREE,K> * info,
            bool usedVersionNumber) {
    
    abtree_Node<DEGREE,K> * volatile * const nodes = info->nodes;
    abtree_SCXRecord<DEGREE,K> * volatile * const scxRecordsSeen = info->scxRecordsSeen;
    const int state = info->state;
    
    // NOW, WE ATTEMPT TO RECLAIM ANY RETIRED NODES AND SCX RECORDS
    // first, we determine how far we got in the loop in help()
    int highestIndexReached = (state == abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED 
            ? info->numberOfNodesToFreeze
            : STATE_GET_HIGHEST_INDEX_REACHED(state));
    const int maxNodes = wrapper_info<DEGREE,K>::MAX_NODES;
//    assert(highestIndexReached>=0);
//    assert(highestIndexReached<=maxNodes);
    
    abtree_SCXRecord<DEGREE,K> *debugSCXRecord = GET_ALLOCATED_SCXRECORD_PTR(tid);
    
    const int state_aborted = abtree_SCXRecord<DEGREE,K>::STATE_ABORTED;
    if (highestIndexReached == 0) {
//        assert(state == state_aborted); /* aborted but only got to help() loop iteration 0 */
        // scx was never inserted into the data structure,
        // so we can reuse it for our next operation.
        return;
    } else {
//        assert(highestIndexReached > 0);
        // For DEBRA+: assuming we're in a quiescent state,
        // it's safe to perform non-restartable operations on bookkeeping data structures
        // (since no other thread will force us to restart in a quiescent state).

        // we wrote a pointer to newscxrecord into the data structure,
        // so we cannot reuse it immediately for our next operation.
        // instead, we allocate a new scx record for our next operation.
//        assert(!recordmgr->supportsCrashRecovery() || recordmgr->isQuiescent(tid));
        if (!usedVersionNumber) {
            REPLACE_ALLOCATED_SCXRECORD(tid);
        }

        // if the state was COMMITTED, then we cannot reuse the nodes the we
        // took from allocatedNodes[], either, so we must replace these nodes.
        // for the chromatic tree, the number of nodes can be found in
        // NUM_INSERTS[operationType].
        // in general, we have to add a parameter, specified when you call SCX,
        // that says how large the replacement subtree of new nodes is.
        // alternatively, we could just move this out into the data structure code,
        // to be performed AFTER an scx completes.
        if (state == abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED) {
            for (int i=0;i<info->numberOfNodesAllocated;++i) {
                REPLACE_ALLOCATED_NODE(tid, i);
            }
        }
        
        // consider the set of scx records for which we will invoke tryRetireSCXRecords,
        // in the following code block.
        // we don't need to call protect object for any of these scx records,
        // because none of them can be retired until we've invoked tryRetireSCXRecord!
        // this is because we changed pointers that pointed to each of these
        // scx records when we performed help(), above.
        // thus, we know they are not retired.
    
        // the scx records in scxRecordsSeen[] may now be retired
        // (since this scx changed each nodes[i]->scxRecord so that it does not
        //  point to any scx record in scxRecordsSeen[].)
        // we start at j=1 because nodes[0] may have been retired and freed
        // since we entered a quiescent state.
        // furthermore, we don't need to check if nodes[0]->left == NULL, since
        // we know nodes[0] is never a leaf.
        for (int j=0;j<highestIndexReached;++j) {
//            DEBUG { // some debug invariant checking
//                if (j>0) { // nodes[0] could be reclaimed already, so nodes[0]->left.load(...) is not safe
//                    if (state == abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED) {
//                        // NOTE: THE FOLLOWING ACCESSES TO NODES[J]'S FIELDS ARE
//                        //       ONLY SAFE BECAUSE STATE IS NOT ABORTED!
//                        //       (also, we are the only one who can invoke retire[j],
//                        //        and we have not done so yet.)
//                        //       IF STATE == ABORTED, THE NODE MAY BE RECLAIMED ALREADY!
//                        if (nodes[j]->isLeaf()) {
//                            if (!(nodes[j]->scxRecord == dummy)) {
////                                COUTATOMICTID("(abtree_SCXRecord<DEGREE,K> *) nodes["<<j<<"]->scxRecord.load(...)="<<((abtree_SCXRecord<DEGREE,K> *) nodes[j]->scxRecord.load(memory_order_relaxed))<<endl);
////                                COUTATOMICTID("dummy="<<dummy<<endl);
//                                assert(false);
//                            }
//                            if (scxRecordsSeen[j] != LLX_RETURN_IS_LEAF) {
////                                if (nodes[j]) { COUTATOMICTID("nodes["<<j<<"]="<<*nodes[j]<<endl); }
////                                else { COUTATOMICTID("nodes["<<j<<"]=NULL"<<endl); }
////                                //if (newNode) { COUTATOMICTID("newNode="<<*newNode<<endl); }
////                                //else { COUTATOMICTID("newNode=NULL"<<endl); }
////                                COUTATOMICTID("scxRecordsSeen["<<j<<"]="<<scxRecordsSeen[j]<<endl);
////                                COUTATOMICTID("dummy="<<dummy<<endl);
//                                assert(false);
//                            }
//                        }
//                    }
//                } else {
//                    assert(scxRecordsSeen[j] != LLX_RETURN_IS_LEAF);
//                }
//            }
            // if nodes[j] is not a leaf, then we froze it, changing the scx record
            // that nodes[j] points to. so, we try to retire the scx record is
            // no longer pointed to by nodes[j].
            // note: we know scxRecordsSeen[j] is not retired, since we have not
            //       zeroed out its flag representing an incoming pointer
            //       from nodes[j] until we execute tryRetireSCXRecord() below.
            //       (it follows that we don't need to invoke protect().)
            if (scxRecordsSeen[j] != LLX_RETURN_IS_LEAF && !IS_VERSION_NUMBER(scxRecordsSeen[j])) {
                bool success = tryRetireSCXRecord(tid, scxRecordsSeen[j], nodes[j]);
//                DEBUG2 {
//                    // note: tryRetireSCXRecord returns whether it retired an scx record.
//                    //       this code checks that we don't retire the same scx record twice!
//                    if (success && scxRecordsSeen[j] != dummy) {
//                        for (int k=j+1;k<highestIndexReached;++k) {
//                            assert(scxRecordsSeen[j] != scxRecordsSeen[k]);
//                        }
//                    }
//                }
            }
        }
        SOFTWARE_BARRIER; // prevent compiler from moving retire() calls before tryRetireSCXRecord() calls above
        if (state == abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED) {
            const int nNodes = info->numberOfNodes;
            // nodes[1], nodes[2], ..., nodes[nNodes-1] are now retired
            for (int j=1;j<nNodes;++j) {
//                DEBUG if (j < highestIndexReached) {
//                    if ((void*) scxRecordsSeen[j] != LLX_RETURN_IS_LEAF) {
//                        assert(nodes[j]->scxRecord == debugSCXRecord);
//                        assert(nodes[j]->marked);
//                    }
//                }
                recordmgr->retire(tid, nodes[j]);
            }
        } else {
//            assert(state >= state_aborted); /* is ABORTED */
        }
    }
}

///**
// * CAN BE INVOKED ONLY IF EVERYTHING FROM THE FIRST LINKED LLX
// * TO THE END OF THIS CALL WILL BE EXECUTED ATOMICALLY
// * (E.G., IN A TXN OR CRITICAL SECTION)
// */
//template<int DEGREE, typename K, class Compare, class RecManager>
//__rtm_force_inline bool abtree<DEGREE,K,Compare,RecManager>::scx_intxn_markingwr_infowr(
//            const int tid,
//            abtree_SCXRecord<DEGREE,K> * info) {
////    abtree_SCXRecord<DEGREE,K>* scx = info;
//    abtree_SCXRecord<DEGREE,K>* scx = (abtree_SCXRecord<DEGREE,K>*) NEXT_VERSION_NUMBER(tid);
//    switch(info->numberOfNodesToFreeze) {
//        case 7: info->nodes[6]->marked = true; info->nodes[6]->scxRecord(scx, memory_order_relaxed);
//        case 6: info->nodes[5]->marked = true; info->nodes[5]->scxRecord(scx, memory_order_relaxed);
//        case 5: info->nodes[4]->marked = true; info->nodes[4]->scxRecord(scx, memory_order_relaxed);
//        case 4: info->nodes[3]->marked = true; info->nodes[3]->scxRecord(scx, memory_order_relaxed);
//        case 3: info->nodes[2]->marked = true; info->nodes[2]->scxRecord(scx, memory_order_relaxed);
//        case 2: info->nodes[1]->marked = true; info->nodes[1]->scxRecord(scx, memory_order_relaxed);
//        case 1: info->nodes[0]->scxRecord(scx, memory_order_relaxed);
//    }
//    info->field = info->newNode;
//    info->state = abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED;
//    return true;
//}

// you may call this only if each node in nodes is protected by a call to recordmgr->protect
template<int DEGREE, typename K, class Compare, class RecManager>
__rtm_force_inline bool abtree<DEGREE,K,Compare,RecManager>::scx(
            const int tid,
            wrapper_info<DEGREE,K> * info) {
//    TRACE COUTATOMICTID("scx(tid="<<tid<<" type="<<info->type<<")"<<endl);
    const int init_state = abtree_SCXRecord<DEGREE,K>::STATE_INPROGRESS;
    abtree_SCXRecord<DEGREE,K> * rec = createSCXRecord(tid, info->field, info->newNode, info->nodes, info->scxRecordsSeen, info->numberOfNodes, info->numberOfNodesToFreeze);
    bool result = help(tid, rec, false) & abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED;
    info->state = rec->state;
//    reclaimMemoryAfterSCX(tid, info);
    return result;
}

// you may call this only if scx is protected by a call to recordmgr->protect.
// each node in scx->nodes must be protected by a call to recordmgr->protect.
// returns the state field of the scx record "scx."
template<int DEGREE, typename K, class Compare, class RecManager>
__rtm_force_inline int abtree<DEGREE,K,Compare,RecManager>::help(const int tid, abtree_SCXRecord<DEGREE,K> *scx, bool helpingOther) {
//    TRACE COUTATOMICTID((helpingOther?"    ":"")<<"help scx@"<<(long long)(void *)scx<<endl);

//    assert(recordmgr->isProtected(tid, scx));
//    assert(scx != dummy);
//    bool updateCAS = false;
    const int nFreeze                                               = scx->numberOfNodesToFreeze;
    const int nNodes                                                = scx->numberOfNodes;
    abtree_Node<DEGREE,K> * volatile * const nodes                  = scx->nodes;
    abtree_SCXRecord<DEGREE,K> * volatile * const scxRecordsSeen    = scx->scxRecordsSeen;
    abtree_Node<DEGREE,K> volatile * const newNode                  = scx->newNode;
//    TRACE COUTATOMICTID("help(tid="<<tid<<" scx="<<*scx<<" helpingOther="<<helpingOther<<"), nFreeze="<<nFreeze<<endl);
    //SOFTWARE_BARRIER; // prevent compiler from reordering read(state) before read(nodes), read(scxRecordsSeen), read(newNode). an x86/64 cpu will not reorder these reads.
    int __state = scx->state;
    if (__state != abtree_SCXRecord<DEGREE,K>::STATE_INPROGRESS) { // TODO: optimize by taking this out, somehow?
        //assert(helpingOther);
        // if state is not in progress here, then helpingOther == true,
        // which means we are helping someone.
        // CONSEQUENTLY, THE RETURN VALUE OF HELP IS IGNORED!
        // !!!!!!!!!!!!!!!!!!!!!!!!!! NO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // this changed when crash recovery was added, because now you can
        // help yourself after being suspected of crashing.
        // in this case, the return value of this function is NOT ignored.
//        TRACE COUTATOMICTID((helpingOther?"    ":"")<<"help return 0 after state != in progress"<<endl);
        //return 0; // return anything since this will be ignored.
        return __state;
    }
    // note: the above cannot cause us to leak the memory allocated for scx,
    // since, if !helpingOther, then we created the SCX record,
    // and did not write it into the data structure.
    // so, no one could have helped us, and state must be INPROGRESS.
    
//    DEBUG {
//        for (int i=0;i<nNodes;++i) {
//            assert(nodes[i] == root || recordmgr->isProtected(tid, nodes[i]));
//        }
//    }
    
    // a note about reclaiming SCX records:
    // IN THEORY, there are exactly three cases in which an SCX record passed
    // to help() is not in the data structure and can be retired.
    //    1. help was invoked directly by SCX, and it failed its first
    //       CAS. in this case the SCX record can be immediately freed.
    //    2. a pointer to an SCX record U with state == COMMITTED is
    //       changed by a CAS to point to a different SCX record.
    //       in this case, the SCX record is retired, but cannot
    //       immediately be freed.
    //     - intuitively, we can retire it because,
    //       after the SCX that created U commits, only the node whose
    //       pointer was changed still points to U. so, when a pointer
    //       that points to U is changed, U is no longer pointed to by
    //       any node in the tree.
    //     - however, a helper or searching process might still have
    //       a local pointer to U, or a local pointer to a
    //       retired node that still points to U.
    //     - so, U can only be freed safely after no process has a
    //       pointer to a retired node that points to U.
    //     - in other words, U can be freed only when all retired nodes
    //       that point to it can be freed.
    //     - if U is retired when case 2 occurs, then it will be retired
    //       AFTER all nodes that point to it are retired. thus, it will
    //       be freed at the same time as, or after, those nodes.
    //    3. a pointer to an SCX record U with state == ABORTED is
    //       changed by a CAS to point to a different SCX record.
    //       this is the hard case, because several nodes in the tree may
    //       point to U.
    //     - in this case, we store the number of pointers from nodes in the
    //       tree to this SCX record in the state field of this SCX record.
    // [NOTE: THE FOLLOWING THREE BULLET POINTS ARE FOR AN OLD IDEA;
    //  THE CURRENT IDEA IS SLIGHTLY DIFFERENT.]    
    //     - when the state of an SCX record becomes STATE_ABORTED, we store
    //       STATE_ABORTED + i in the state field, where i is the number of
    //       incoming pointers from nodes in the tree. (STATE_INPROGRESS and
    //       STATE_COMMITTED are both less than STATE_ABORTED.)
    //     - every time we change a pointer from an SCX record U to another
    //       SCX record U', and U.state > STATE_ABORTED, we decrement U.state.
    //     - if U.state == STATE_ABORTED, then we know there are no incoming
    //       pointers to U from nodes in the tree, so we can retire U.
    //
    // HOWEVER, in practice, we don't freeze leaves for insert and delete,
    // so we have to be careful to deal with a possible memory leak.
    // if some operations (e.g., rebalancing steps) DO freeze leaves, then
    // we can wind up in a situation where a rebalancing step freezes a leaf
    // and is aborted, then a successful insertion or deletion retires
    // that leaf without freezing it. in this scenario, the scx record
    // for the rebalancing step will never be retired, since no further
    // freezing CAS will modify it's scx record pointer (which means it will
    // never trigger case 3, above).
    // there are three (easy) possible fixes for this problem.
    //   1. make sure all operations freeze leaves
    //   2. make sure no operation freezes leaves
    //   3. when retiring a node, if it points to an scx record with
    //      state aborted, then respond as if we were in case 3, above.
    //      (note: since the dummy scx record has state ABORTED,
    //       we have to be a little bit careful; we ignore the dummy.)
    // in this implementation, we choose option 2. this is viable because
    // leaves are immutable, and, hence, do not need to be frozen.
    
    // freeze sub-tree
    int flags = 1; // bit i is 1 if nodes[i] is not a leaf, and 0 otherwise.
    // note that flags bit 0 is always set, since nodes[0] is never a leaf.
    // (technically, if we abort in the first iteration,
    //  flags=1 makes no sense (since it suggests there is one pointer to scx
    //  from a node in the tree), but in this case we ignore the flags variable.)
    for (int i=helpingOther; i<nFreeze; ++i) {
        if (scxRecordsSeen[i] == LLX_RETURN_IS_LEAF) {
//            TRACE COUTATOMICTID((helpingOther?"    ":"")<<"nodes["<<i<<"] is a leaf\n");
//            assert(i > 0); // nodes[0] cannot be a leaf...
            continue; // do not freeze leaves
        }
        
        bool successfulCAS = __sync_bool_compare_and_swap(&nodes[i]->scxRecord, scxRecordsSeen[i], scx);
        abtree_SCXRecord<DEGREE,K> * exp = nodes[i]->scxRecord;
        if (!successfulCAS && exp != scx) { // if work was not done

//        uintptr_t exp = (uintptr_t) scxRecordsSeen[i];
//        bool successfulCAS = nodes[i]->scxRecord.compare_exchange_strong(exp, (uintptr_t) scx);     // MEMBAR ON X86/64
//        if (!successfulCAS && (abtree_SCXRecord<DEGREE,K> *) exp != scx) { // if work was not done
            if (scx->allFrozen) {
//                assert(scx->state == 1); /*STATE_COMMITTED*/
//                TRACE COUTATOMICTID((helpingOther?"    ":"")<<"help return COMMITTED after failed freezing cas on nodes["<<i<<"]"<<endl);
                return abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED; // success
            } else {
                if (i == 0) {
                    // if i == 0, then our scx record was never in the tree, and,
                    // consequently, no one else can have a pointer to it.
                    // so, there is no need to change scx->state.
                    // (recall that helpers start with helpingOther == true,
                    //  so i>0 for every helper. thus, if and only if i==0,
                    //  we created this scx record and failed our first CAS.)
//                    assert(!helpingOther);
//                    TRACE COUTATOMICTID((helpingOther?"    ":"")<<"help return ABORTED after failed freezing cas on nodes["<<i<<"]"<<endl);
                    scx->state = ABORT_STATE_INIT(0, 0); // scx is aborted (but no one else will ever know)
                    return ABORT_STATE_INIT(0, 0);
                } else {
                    // if this is the first failed freezing CAS to occur for this SCX,
                    // then flags encodes the pointers to this scx record from nodes IN the tree.
                    // (the following CAS will succeed only the first time it is performed
                    //  by any thread running help() for this scx.)
                    int expectedState = abtree_SCXRecord<DEGREE,K>::STATE_INPROGRESS;
                    int newState = ABORT_STATE_INIT(i, flags);
                    bool success = __sync_bool_compare_and_swap(&scx->state, expectedState, newState);     // MEMBAR ON X86/64
                    expectedState = scx->state;
//                    assert(expectedState != 1); /* not committed */ // only valid if expectedState contains the current value after the CAS (as it does with the C++ atomic.h CAS function)
                    // note2: a regular write will not do, here, since two people can start helping, one can abort at i>0, then after a long time, the other can fail to CAS i=0, so they can get different i values.
                    const int state_aborted = abtree_SCXRecord<DEGREE,K>::STATE_ABORTED; // alias needed since the :: causes problems with the assert() macro, below
//                    assert(expectedState & state_aborted);
                    // ABORTED THE SCX AFTER PERFORMING ONE OR MORE SUCCESSFUL FREEZING CASs
                    if (success) {
//                        TRACE COUTATOMICTID((helpingOther?"    ":"")<<"help return ABORTED(changed to "<<newState<<") after failed freezing cas on nodes["<<i<<"]"<<endl);
                        return newState;
                    } else {
//                        TRACE COUTATOMICTID((helpingOther?"    ":"")<<"help return ABORTED(failed to change to "<<newState<<" because encountered "<<expectedState<<" instead of in progress) after failed freezing cas on nodes["<<i<<"]"<<endl);
                        return expectedState; // this has been overwritten by compare_exchange_strong with the value that caused the CAS to fail.
                    }
                }
            }
        } else {
            flags |= (1<<i); // nodes[i] was frozen for scx
            const int state_inprogress = abtree_SCXRecord<DEGREE,K>::STATE_INPROGRESS;
//            assert(exp == scx || (exp->state != state_inprogress));
        }
    }
    scx->allFrozen = true;
    // note: i think the sequential consistency memory model is not actually needed here...
    // why? in an execution where no reads are moved before allFrozen by the
    // compiler/cpu (because we added a barrier here), any process that sees
    // allFrozen = true has also just seen that nodes[i]->op != &op,
    // which means that the operation it is helping has already completed!
    // in particular, the child CAS will already have been done, which implies
    // that allFrozen will have been set to true, since the compiler/cpu cannot
    // move the (first) child CAS before the (first) write to allFrozen.
    SOFTWARE_BARRIER;
    for (int i=1; i<nFreeze; ++i) {
        if (scxRecordsSeen[i] == LLX_RETURN_IS_LEAF) continue; // do not mark leaves
        nodes[i]->marked = true; // finalize all but first node
    }

    // CAS in the new sub-tree (update CAS)
    abtree_Node<DEGREE,K> * expected = nodes[1];
    //scx->field->compare_exchange_strong(expected, (uintptr_t) newNode);                             // MEMBAR ON X86/64
    __sync_bool_compare_and_swap(scx->field, expected, newNode);
//    assert(scx->state < 2); // not aborted
    scx->state = abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED;
//    TRACE COUTATOMICTID((helpingOther?"    ":"")<<"scx@"<<(long long)(void*)scx<<" has state"<<scx->state.load(memory_order_relaxed)<<" @ "<<(long long)(void *)(&scx->state)<<endl);
    
//    TRACE COUTATOMICTID((helpingOther?"    ":"")<<"help return COMMITTED after performing update cas"<<endl);
    return abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED; // success
}

//template<int DEGREE, typename K, class Compare, class RecManager>
//__rtm_force_inline void *abtree<DEGREE,K,Compare,RecManager>::llx_intxn_markingwr_infowr(
//            const int tid,
//            abtree_Node<DEGREE,K> *node,
//            void **retPointers) {
//    abtree_SCXRecord<DEGREE,K> *scx1 = node->scxRecord;
//    int state = (IS_VERSION_NUMBER(scx1) ? abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED : scx1->state);
//    bool marked = node->marked;
//    SOFTWARE_BARRIER;
//    if (marked) {
//        return NULL;
//    } else {
//        if ((state & abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED /*&& !marked*/) || state & abtree_SCXRecord<DEGREE,K>::STATE_ABORTED) {
//            if (node->isLeaf()) {
//                // if node is a leaf, we return a special value
//                return (void*) LLX_RETURN_IS_LEAF;
//            } else {
//                // otherwise, we read all mutable fields
//                for (int i=0;i<node->size;++i) {
//                    retPointers[i] = node->ptrs[i];
//                }
//            }
//            return scx1;
//        }
//    }
//    return NULL; // fail
//}

// you may call this only if node is protected by a call to recordmgr->protect
template<int DEGREE, typename K, class Compare, class RecManager>
__rtm_force_inline void *abtree<DEGREE,K,Compare,RecManager>::llx(
            const int tid,
            abtree_Node<DEGREE,K> *node,
            void **retPointers) {
//    TRACE COUTATOMICTID("llx(tid="<<tid<<", node="<<*node<<")"<<endl);
//    assert(node == root || recordmgr->isProtected(tid, node));
    abtree_SCXRecord<DEGREE,K> *scx1 = node->scxRecord;
//    abtree_SCXRecord<DEGREE,K> * info;
//    IF_FAIL_TO_PROTECT_SCX(info, tid, scx1, &node->scxRecord, &node->marked) {
//        TRACE COUTATOMICTID("llx return1 (tid="<<tid<<" key="<<node->key<<")\n");
//        DEBUG counters->llxFail->inc(tid);
//        return NULL;
//    } // return and retry
//    assert(scx1 == dummy || recordmgr->isProtected(tid, scx1));
//    TRACE COUTATOMICTID("scx@"<<(long long)(void*)scx1<<" has state"<<scx1->state<<" @ "<<(long long)(void *)(&scx1->state)<<endl);
    int state = (IS_VERSION_NUMBER(scx1) ? abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED : scx1->state);
//    TRACE COUTATOMICTID("state="<<state<<" IS_VERSION_NUMBER(scx1)="<<IS_VERSION_NUMBER(scx1)<<endl);
    SOFTWARE_BARRIER;       // prevent compiler from moving the read of marked before the read of state (no hw barrier needed on x86/64, since there is no read-read reordering)
    int marked = node->marked;
    SOFTWARE_BARRIER;       // prevent compiler from moving the reads scx2=node->scxRecord or scx3=node->scxRecord before the read of marked. (no h/w barrier needed on x86/64 since there is no read-read reordering)
    if ((state & abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED && !marked) || state & abtree_SCXRecord<DEGREE,K>::STATE_ABORTED) {
        SOFTWARE_BARRIER;       // prevent compiler from moving the reads scx2=node->scxRecord or scx3=node->scxRecord before the read of marked. (no h/w barrier needed on x86/64 since there is no read-read reordering)
        if (node->isLeaf()) {
            // if node is a leaf, we return a special value
//            TRACE COUTATOMICTID("llx return is leaf"<<endl);
            return (void *) LLX_RETURN_IS_LEAF;
        } else {
            // otherwise, we read all mutable fields
            for (int i=0;i<node->size;++i) {
                retPointers[i] = node->ptrs[i];
            }
        }
        SOFTWARE_BARRIER; // prevent compiler from moving the read of node->scxRecord before the reads of node's mutable fields
        abtree_SCXRecord<DEGREE,K> *scx2 = node->scxRecord;
        if (scx1 == scx2) {
//            DEBUG {
//                if (!IS_VERSION_NUMBER(scx1) && marked && state & abtree_SCXRecord<DEGREE,K>::STATE_ABORTED) {
//                    // since scx1 == scx2, the two claims in the antecedent hold simultaneously.
//                    assert(scx1 == dummy || recordmgr->isProtected(tid, scx1));
//                    assert(node == root || recordmgr->isProtected(tid, node));
//                    assert(node->marked.load(memory_order_relaxed));
//                    assert(scx1->state.load(memory_order_relaxed) & 2 /* aborted */);
//                    assert(false);
//                }
//            }
//            TRACE COUTATOMICTID("llx return snapshot (tid="<<tid<<" state="<<state<<" marked="<<marked<<" scx1="<<(long long)(void*)scx1<<")\n"); 
//            DEBUG counters->llxSuccess->inc(tid);
//            if (scx1 != dummy) recordmgr->unprotect(tid, scx1);
            // on x86/64, we do not need any memory barrier here to prevent mutable fields of node from being moved before our read of scx1, because the hardware does not perform read-read reordering. on another platform, we would need to ensure no read from after this point is reordered before this point (technically, before the read that becomes scx1)...
            return scx1;    // success
        } else {
//            DEBUG {
//                IF_FAIL_TO_PROTECT_SCX(info, tid, scx2, &node->scxRecord, &node->marked) {
//                    TRACE COUTATOMICTID("llx return1.b (tid="<<tid<<" key="<<node->key<<")\n");
//                    DEBUG counters->llxFail->inc(tid);
//                    return NULL;
//                } else {
//                    assert(scx1 == dummy || recordmgr->isProtected(tid, scx1));
//                    assert(recordmgr->isProtected(tid, scx2));
//                    assert(node == root || recordmgr->isProtected(tid, node));
//                    int __state = scx2->state.load(memory_order_relaxed);
//                    abtree_SCXRecord<DEGREE,K>* __scx = (abtree_SCXRecord<DEGREE,K>*) node->scxRecord.load(memory_order_relaxed);
//                    if (!IS_VERSION_NUMBER(__scx) && (marked && __state & 2 && __scx == scx2)) {
//                        COUTATOMICTID("ERROR: marked && state aborted! raising signal SIGTERM..."<<endl);
//                        COUTATOMICTID("node      = "<<*node<<endl);
//                        COUTATOMICTID("scx2      = "<<*scx2<<endl);
//                        COUTATOMICTID("state     = "<<state<<" bits="<<bitset<32>(state)<<endl);
//                        COUTATOMICTID("marked    = "<<marked<<endl);
//                        COUTATOMICTID("__state   = "<<__state<<" bits="<<bitset<32>(__state)<<endl);
//                        assert(node->marked.load(memory_order_relaxed));
//                        assert(scx2->state.load(memory_order_relaxed) & 2 /* aborted */);
//                        raise(SIGTERM);
//                    }
////                    recordmgr->unprotect(tid, scx2);
//                }
//            }
            if (recordmgr->shouldHelp()) {
//                IF_FAIL_TO_PROTECT_SCX(info, tid, scx2, &node->scxRecord, &node->marked) {
//                    TRACE COUTATOMICTID("llx return3 (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<")\n");
//                    DEBUG counters->llxFail->inc(tid);
//                    return NULL;
//                } // return and retry
//                assert(scx2 != dummy);
//                assert(recordmgr->isProtected(tid, scx2));
//                TRACE COUTATOMICTID("llx help 1 scxrecord@"<<(long long)(void *)scx2<<endl);
                if (!IS_VERSION_NUMBER(scx2)) {
                    help(tid, scx2, true);
                }
//                if (scx2 != dummy) recordmgr->unprotect(tid, scx2);
            }
        }
//        if (scx1 != dummy) recordmgr->unprotect(tid, scx1);
    } else if (state == abtree_SCXRecord<DEGREE,K>::STATE_INPROGRESS) {
        if (recordmgr->shouldHelp()) {
//            assert(scx1 != dummy);
//            assert(recordmgr->isProtected(tid, scx1));
//            TRACE COUTATOMICTID("llx help 2 scxrecord@"<<(long long)(void *)scx1<<endl);
            if (!IS_VERSION_NUMBER(scx1)) {
                help(tid, scx1, true);
            }
        }
//        if (scx1 != dummy) recordmgr->unprotect(tid, scx1);
    } else {
        // state committed and marked
//        assert(state == 1); /* abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED */
//        assert(marked);
        if (recordmgr->shouldHelp()) {
            abtree_SCXRecord<DEGREE,K> *scx3 = node->scxRecord;
//            if (scx3 == dummy) {
//                COUTATOMICTID("scx1="<<scx1<<endl);
//                COUTATOMICTID("scx3="<<scx3<<endl);
//                COUTATOMICTID("dummy="<<dummy<<endl);
//                COUTATOMICTID("node="<<*node<<endl);
//            }
////            if (scx1 != dummy) recordmgr->unprotect(tid, scx1);
////            IF_FAIL_TO_PROTECT_SCX(info, tid, scx3, &node->scxRecord, &node->marked) {
////                TRACE COUTATOMICTID("llx return4 (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<")\n");
////                DEBUG counters->llxFail->inc(tid);
////                return NULL;
////            } // return and retry
//            assert(scx3 != dummy);
//            assert(recordmgr->isProtected(tid, scx3));
//            TRACE COUTATOMICTID("llx help 3 scxrecord@"<<(long long)(void *)scx3<<endl);
            if (!IS_VERSION_NUMBER(scx3)) {
                help(tid, scx3, true);
            }
//            if (scx3 != dummy) recordmgr->unprotect(tid, scx3);
        } else {
//            if (scx1 != dummy) recordmgr->unprotect(tid, scx1);
        }
    }
//    TRACE COUTATOMICTID("llx return5 (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<")\n");
//    DEBUG counters->llxFail->inc(tid);
//    TRACE COUTATOMICTID("llx returns NULL because of node with scxrecord@"
//            <<(long long)(void *)node->scxRecord
//            <<" (already read="<<(long long)(void *)scx1<<")"
//            <<" with state "<<scx1->state
//            <<" (already read="<<state<<")"
//            <<" and mark-bit "<<node->marked<<" (already read="<<marked<<")"
//            <<endl);
    return NULL;            // fail
}

/**
 * CAN BE INVOKED ONLY IF EVERYTHING FROM THE FIRST LINKED LLX
 * TO THE END OF THIS CALL WILL BE EXECUTED IN A TXN
 */
template<int DEGREE, typename K, class Compare, class RecManager>
__rtm_force_inline bool abtree<DEGREE,K,Compare,RecManager>::scx_txn(
            const int tid,
            wrapper_info<DEGREE,K> * info) {
    abtree_SCXRecord<DEGREE,K> * scx = (abtree_SCXRecord<DEGREE,K> *) NEXT_VERSION_NUMBER(tid);
    for (int i=0;i<info->numberOfNodesToFreeze;++i) {
        if (info->scxRecordsSeen[i] == LLX_RETURN_IS_LEAF) continue; // do not freeze leaves
        info->nodes[i]->scxRecord = scx;
    }
    for (int i=1;i<info->numberOfNodes;++i) {
        if (info->scxRecordsSeen[i] == LLX_RETURN_IS_LEAF) continue; // do not mark leaves
        info->nodes[i]->marked = true;
    }
    *(info->field) = (void*) info->newNode;
    info->state = abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED;
    return true;
}

template<int DEGREE, typename K, class Compare, class RecManager>
__rtm_force_inline void * abtree<DEGREE,K,Compare,RecManager>::llx_txn(
            const int tid,
            abtree_Node<DEGREE,K> * node,
            void **retPointers) {
    abtree_SCXRecord<DEGREE,K> *scx1 = node->scxRecord;
    int state = (IS_VERSION_NUMBER(scx1) ? abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED : scx1->state);
    bool marked = node->marked;
    if (marked) {
//        /** DEBUG **/ XEND(); cout<<"ERROR: ENCOUNTERED MARKED NODE"<<endl; exit(-1);
        return NULL;
    } else if (state) {
        //if ((state & abtree_SCXRecord<DEGREE,K>::STATE_COMMITTED /*&& !marked*/)
        //        || state & abtree_SCXRecord<DEGREE,K>::STATE_ABORTED) {
        if (node->isLeaf()) {   // if node is a leaf, we return a special value
            return (void *) LLX_RETURN_IS_LEAF;
        } else {                // otherwise, we read all mutable fields
            for (int i=0;i<node->size;++i) {
                retPointers[i] = node->ptrs[i];
            }
            return scx1;
        }
    }
//    /** DEBUG **/ XEND(); cout<<"ERROR: ENCOUNTERED NODE WITH STATE "<<state<<" VERSION#? "<<IS_VERSION_NUMBER(scx1)<<endl; exit(-1);
    return NULL; // fail
}

#endif	/* ABTREE_IMPL_H */

