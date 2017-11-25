/**
 * Preliminary C++ implementation of unbalanced binary search tree using LLX/SCX.
 *
 * Copyright (C) 2014 Trevor Brown
 * This preliminary implementation is CONFIDENTIAL and may not be distributed.
 */

/**
 * Note: costly code to reclaim SCX records is made unnecessary by our transformation!
 */

#include "bst.h"
#include <cassert>
#include <cstdlib>
#include "../globals_extern.h"
#include "../globals.h"
using namespace std;

#ifdef NOREBALANCING
#define IFREBALANCING if (0)
#else
#define IFREBALANCING if (1)
#endif

#if defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__)
#   define LWSYNC asm volatile("lwsync" ::: "memory")
#   define SYNC asm volatile("sync" ::: "memory")
#   define SYNC_RMW asm volatile("sync" ::: "memory")
#elif defined(__x86_64__) || defined(_M_X64)
#   define LWSYNC /* not needed */
#   define SYNC __sync_synchronize()
#   define SYNC_RMW /* not needed */
#endif

template<class K, class V, class Compare, class RecManager>
SCXRecord<K,V>* bst<K,V,Compare,RecManager>::allocateSCXRecord(
            const int tid) {
    SCXRecord<K,V> *newop = recmgr->template allocate<SCXRecord<K,V> >(tid);
    if (newop == NULL) {
        COUTATOMICTID("ERROR: could not allocate scx record"<<endl);
        exit(-1);
    }
    return newop;
}

template<class K, class V, class Compare, class RecManager>
SCXRecord<K,V>* bst<K,V,Compare,RecManager>::initializeSCXRecord(
            const int tid,
            SCXRecord<K,V> * const newop,
            ReclamationInfo<K,V> * const info,
            atomic_uintptr_t * const field,
            Node<K,V> * const newNode) {
    //newop->type = info->type;
    newop->newNode = newNode;
//    memcpy(newop->nodes, nodes, sizeof(Node<K,V>*)*NUM_OF_NODES[type]);
//    memcpy(newop->scxRecordsSeen, llxResults, sizeof(SCXRecord<K,V>*)*NUM_TO_FREEZE[type]);
    for (int i=0;i<info->numberOfNodes;++i) {
        newop->nodes[i] = info->nodes[i];
    }
    for (int i=0;i<info->numberOfNodesToFreeze;++i) {
        newop->scxRecordsSeen[i] = (SCXRecord<K,V> *) info->llxResults[i];
    }
    // note: synchronization is not necessary for the following accesses,
    // since a memory barrier will occur before this object becomes reachable
    // from an entry point to the data structure.
    newop->state.store(SCXRecord<K,V>::STATE_INPROGRESS, memory_order_relaxed);
    newop->allFrozen.store(false, memory_order_relaxed);
    newop->field = field;
    newop->numberOfNodes = (char) info->numberOfNodes;
    newop->numberOfNodesToFreeze = (char) info->numberOfNodesToFreeze;
    return newop;
}

template<class K, class V, class Compare, class RecManager>
Node<K,V>* bst<K,V,Compare,RecManager>::allocateNode(
            const int tid) {
    Node<K,V> *newnode = recmgr->template allocate<Node<K,V> >(tid);
    if (newnode == NULL) {
        COUTATOMICTID("ERROR: could not allocate node"<<endl);
        exit(-1);
    }
    return newnode;
}

template<class K, class V, class Compare, class RecManager>
Node<K,V>* bst<K,V,Compare,RecManager>::initializeNode(
            const int tid,
            Node<K,V> * const newnode,
            const K& key,
            const V& value,
            Node<K,V> * const left,
            Node<K,V> * const right) {
    newnode->key = key;
    newnode->value = value;
//    newnode->weight = weight;
    // note: synchronization is not necessary for the following accesses,
    // since a memory barrier will occur before this object becomes reachable
    // from an entry point to the data structure.
    newnode->left.store((uintptr_t) left, memory_order_relaxed);
    newnode->right.store((uintptr_t) right, memory_order_relaxed);
    newnode->scxRecord.store((uintptr_t) dummy, memory_order_relaxed);
    newnode->marked.store(false, memory_order_relaxed);
    return newnode;
}

template<class K, class V, class Compare, class RecManager>
long long bst<K,V,Compare,RecManager>::debugKeySum(Node<K,V> * node) {
    if (node == NULL) return 0;
    if ((void*) node->left.load(memory_order_relaxed) == NULL) return (long long) node->key;
    return debugKeySum((Node<K,V> *) node->left.load(memory_order_relaxed))
         + debugKeySum((Node<K,V> *) node->right.load(memory_order_relaxed));
}

template<class K, class V, class Compare, class RecManager>
bool bst<K,V,Compare,RecManager>::validate(Node<K,V> * const node, const int currdepth, const int leafdepth) {
    return true;
}

template<class K, class V, class Compare, class RecManager>
bool bst<K,V,Compare,RecManager>::validate(const long long keysum, const bool checkkeysum) {
    return true;
}

template<class K, class V, class Compare, class RecManager>
inline int bst<K,V,Compare,RecManager>::size() {
    return computeSize((Node<K,V> *) ((Node<K,V> *) root->left.load(memory_order_relaxed))->left.load(memory_order_relaxed));
}
    
template<class K, class V, class Compare, class RecManager>
inline int bst<K,V,Compare,RecManager>::computeSize(Node<K,V> * const root) {
    if (root == NULL) return 0;
    if ((Node<K,V> *) root->left.load(memory_order_relaxed) != NULL) { // if internal node
        return computeSize((Node<K,V> *) root->left.load(memory_order_relaxed))
                + computeSize((Node<K,V> *) root->right.load(memory_order_relaxed));
    } else { // if leaf
        return 1;
//        printf(" %d", root->key);
    }
}

template<class K, class V, class Compare, class RecManager>
bool bst<K,V,Compare,RecManager>::contains(const int tid, const K& key) {
    pair<V,bool> result = find(tid, key);
    return result.second;
}

template<class K, class V, class Compare, class RecManager>
int bst<K,V,Compare,RecManager>::rangeQuery(const int tid, const K& low, const K& hi, Node<K,V> const ** result) {
    int cnt;
    void *input[] = {(void*) &low, (void*) &hi};
    void *output[] = {(void*) result, (void*) &cnt};
    
    ReclamationInfo<K,V> info;
    bool finished = 0;
    for (;;) {
        recmgr->leaveQuiescentState(tid);
        finished = rangeQuery_vlx(&info, tid, input, output);
        recmgr->enterQuiescentState(tid);
        if (finished) {
            break;
        }
    }
    return cnt;
}

template<class K, class V, class Compare, class RecManager>
int bst<K,V,Compare,RecManager>::rangeQuery_vlx(ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) {
//    COUTATOMICTID("rangeQuery(low="<<low<<", hi="<<hi<<", size="<<size<<")"<<endl);

    Node<K,V> const ** result = (Node<K,V> const **) output[0];
    int *cnt = (int*) output[1];
    const K& low = *((const K*) input[0]);
    const K& hi = *((const K*) input[1]);
    
    block<Node<K,V> > stack (NULL);
    
//retry:
//    stack.clearWithoutFreeingElements();
    *cnt = 0;

    // depth first traversal (of interesting subtrees)
    stack.push(root);
    while (!stack.isEmpty()) {
        Node<K,V> * node = stack.pop();
        Node<K,V> * left;
        Node<K,V> * right;
        
        //COUTATOMICTID("    visiting node "<<*node<<endl);
        // if llx on node fails, then retry
        if (llx(tid, node, &left, &right) == NULL) { // marked bit checked in here
            //cout<<"Retry because of failed llx\n";
//            goto retry; // abort because of concurrency
            return false;

        // else if internal node, explore its children
        } else if (left != NULL) {
            //COUTATOMICTID("    internal node key="<<node->key<<" low="<<low<<" hi="<<hi<<" cmp(hi, node->key)="<<cmp(hi, node->key)<<" cmp(low, node->key)="<<cmp(low, node->key)<<endl);
            if (node->key != this->NO_KEY && !cmp(hi, node->key)) {
                //COUTATOMICTID("    stack.push right: "<<right<<endl);
                stack.push(right);
            }
            if (node->key == this->NO_KEY || cmp(low, node->key)) {
                //COUTATOMICTID("    stack.push left: "<<left<<endl);
                stack.push(left);
            }
            
        // else if leaf node, check if we should return it
        } else {
            //COUTATOMICTID("    leaf node"<<endl);
            //visitedNodes[cnt] = node;
            if (node->key != this->NO_KEY && !cmp(node->key, low) && !cmp(hi, node->key)) {
                //COUTATOMICTID("    result["<<cnt<<"] = node "<<node<<endl);
                result[(*cnt)++] = node;
            }
        }
    }
    // validation
    for (int i=0;i<*cnt;++i) {
        if (result[i]->marked.load(memory_order_relaxed)) {
            //cout<<"Retry because of failed validation, return set size "<<cnt<<endl;
//            goto retry; // abort because of concurrency
            return false;
        }
    }
    
    // success
    return true;
}

template<class K, class V, class Compare, class RecManager>
const pair<V,bool> bst<K,V,Compare,RecManager>::find(const int tid, const K& key) {
    pair<V,bool> result;
    bst_retired_info info;
    Node<K,V> *p;
    Node<K,V> *l;
    for (;;) {
        TRACE COUTATOMICTID("find(tid="<<tid<<" key="<<key<<")"<<endl);
        recmgr->leaveQuiescentState(tid);
        p = (Node<K,V>*) root->left.load(memory_order_relaxed);
        l = (Node<K,V>*) p->left.load(memory_order_relaxed);
        if (l == NULL) {
            result = pair<V,bool>(NO_VALUE, false); // no keys in data structure
            recmgr->enterQuiescentState(tid);
            return result; // success
        }

        while ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL) {
            TRACE COUTATOMICTID("traversing tree; l="<<*l<<endl);
            p = l; // note: the new p is currently protected
            assert(p->key != NO_KEY);
            if (cmp(key, p->key)) {
                l = (Node<K,V>*) p->left.load(memory_order_relaxed);
            } else {
                l = (Node<K,V>*) p->right.load(memory_order_relaxed);
            }
        }
        if (key == l->key) {
            result = pair<V,bool>(l->value, true);
        } else {
            result = pair<V,bool>(NO_VALUE, false);
        }
        recmgr->enterQuiescentState(tid);
        return result; // success
    }
    return pair<V,bool>(NO_VALUE, false);
}

template<class K, class V, class Compare, class RecManager>
const V bst<K,V,Compare,RecManager>::insert(const int tid, const K& key, const V& val) {
    bool onlyIfAbsent = false;
    V result = NO_VALUE;
    void *input[] = {(void*) &key, (void*) &val, (void*) &onlyIfAbsent};
    void *output[] = {(void*) &result};

    ReclamationInfo<K,V> info;
    bool finished = 0;
    for (;;) {
        recmgr->leaveQuiescentState(tid);
        finished = updateInsert_search_llx_scx(&info, tid, input, output);
        recmgr->enterQuiescentState(tid);
        if (finished) {
            break;
        }
    }
    return result;
}

template<class K, class V, class Compare, class RecManager>
const pair<V,bool> bst<K,V,Compare,RecManager>::erase(const int tid, const K& key) {
    V result = NO_VALUE;
    void *input[] = {(void*) &key};
    void *output[] = {(void*) &result};
    
    ReclamationInfo<K,V> info;
    bool finished = 0;
    for (;;) {
        recmgr->leaveQuiescentState(tid);
        finished = updateErase_search_llx_scx(&info, tid, input, output);
        recmgr->enterQuiescentState(tid);
        if (finished) {
            break;
        }
    }
    return pair<V,bool>(result, (result != NO_VALUE));
}

template<class K, class V, class Compare, class RecManager>
inline bool bst<K,V,Compare,RecManager>::updateInsert_search_llx_scx(
            ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) {
    const K& key = *((const K*) input[0]);
    const V& val = *((const V*) input[1]);
    const bool onlyIfAbsent = *((const bool*) input[2]);
    V *result = (V*) output[0];
    
    TRACE COUTATOMICTID("updateInsert_search_llx_scx(tid="<<tid<<", key="<<key<<")"<<endl);
    
    Node<K,V> *p = root, *l;
    l = (Node<K,V>*) root->left.load(memory_order_relaxed);
    if ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL) { // the tree contains some node besides sentinels...
        p = l;
        l = (Node<K,V>*) l->left.load(memory_order_relaxed);    // note: l must have key infinity, and l->left must not.
        while ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL) {
            p = l;
            if (cmp(key, p->key)) {
                l = (Node<K,V>*) p->left.load(memory_order_relaxed);
            } else {
                l = (Node<K,V>*) p->right.load(memory_order_relaxed);
            }
        }
    }
    // if we find the key in the tree already
    if (key == l->key) {
        if (onlyIfAbsent) {
            TRACE COUTATOMICTID("return true5\n");
            *result = val; // for insertIfAbsent, we don't care about the particular value, just whether we inserted or not. so, we use val to signify not having inserted (and NO_VALUE to signify having inserted).
            return true; // success
        }
        Node<K,V> *pleft, *pright;
        if ((info->llxResults[0] = llx(tid, p, &pleft, &pright)) == NULL) {
            return false;
        } //RETRY;
        if (l != pleft && l != pright) {
            return false;
        } //RETRY;
        *result = l->value;
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, /*l->weight,*/ NULL, NULL);
        info->numberOfNodes = 2;
        info->numberOfNodesToFreeze = 1;
        info->numberOfNodesToReclaim = 1; // only reclaim l (reclamation starts at nodes[1])
        info->numberOfNodesAllocated = 1;
        info->type = SCXRecord<K,V>::TYPE_REPLACE;
        info->nodes[0] = p;
        info->nodes[1] = l;
        bool retval = scx(tid, info, (l == pleft ? &p->left : &p->right), GET_ALLOCATED_NODE_PTR(tid, 0));
        if (retval) {
//            counters->updateChange[info->path]->inc(tid);
        }
        return retval;
    } else {
        Node<K,V> *pleft, *pright;
        if ((info->llxResults[0] = llx(tid, p, &pleft, &pright)) == NULL) {
            return false;
        } //RETRY;
        if (l != pleft && l != pright) {
            return false;
        } //RETRY;
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, NULL, NULL);
//        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), l->key, l->value, /*1,*/ NULL, NULL);
        // TODO: change all equality comparisons with NO_KEY to use cmp()
        if (l->key == NO_KEY || cmp(key, l->key)) {
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), l->key, l->value, GET_ALLOCATED_NODE_PTR(tid, 0), l);
        } else {
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), key, val, l, GET_ALLOCATED_NODE_PTR(tid, 0));
        }
        *result = NO_VALUE;
        info->numberOfNodes = 2;
        info->numberOfNodesToReclaim = 0;
        info->numberOfNodesToFreeze = 1; // only freeze nodes[0]
        info->numberOfNodesAllocated = 2;
        info->type = SCXRecord<K,V>::TYPE_INS;
        info->nodes[0] = p;
        info->nodes[1] = l; // note: used as OLD value for CAS that changes p's child pointer (but is not frozen or marked)
        bool retval = scx(tid, info, (l == pleft ? &p->left : &p->right), GET_ALLOCATED_NODE_PTR(tid, 1));
        if (retval) {
//            counters->updateChange[info->path]->inc(tid);
        }
        return retval;
    }
}

template<class K, class V, class Compare, class RecManager>
inline bool bst<K,V,Compare,RecManager>::updateErase_search_llx_scx(
            ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) { // input consists of: const K& key
    const K& key = *((const K*) input[0]);
    V *result = (V*) output[0];
//    bool *shouldRebalance = (bool*) output[1];

    TRACE COUTATOMICTID("updateErase_search_llx_scx(tid="<<tid<<", key="<<key<<")"<<endl);

    Node<K,V> *gp, *p, *l;
    l = (Node<K,V>*) root->left.load(memory_order_relaxed);
    if ((Node<K,V>*) l->left.load(memory_order_relaxed) == NULL) {
        *result = NO_VALUE;
        return true;
    } // only sentinels in tree...
    gp = root;
    p = l;
    l = (Node<K,V>*) p->left.load(memory_order_relaxed);    // note: l must have key infinity, and l->left must not.
    while ((Node<K,V>*) l->left.load(memory_order_relaxed) != NULL) {
        gp = p;
        p = l;
        if (cmp(key, p->key)) {
            l = (Node<K,V>*) p->left.load(memory_order_relaxed);
        } else {
            l = (Node<K,V>*) p->right.load(memory_order_relaxed);
        }
    }
    // if we fail to find the key in the tree
    if (key != l->key) {
        *result = NO_VALUE;
        return true; // success
    } else {
        Node<K,V> *gpleft, *gpright;
        Node<K,V> *pleft, *pright;
        Node<K,V> *sleft, *sright;
        if ((info->llxResults[0] = llx(tid, gp, &gpleft, &gpright)) == NULL) return false;
        if (p != gpleft && p != gpright) return false;
        if ((info->llxResults[1] = llx(tid, p, &pleft, &pright)) == NULL) return false;
        if (l != pleft && l != pright) return false;
        *result = l->value;
        // Read fields for the sibling s of l
        Node<K,V> *s = (l == pleft ? pright : pleft);
        if ((info->llxResults[2] = llx(tid, s, &sleft, &sright)) == NULL) return false;
        // Now, if the op. succeeds, all structure is guaranteed to be just as we verified
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), s->key, s->value, /*newWeight,*/ sleft, sright);
        info->numberOfNodes = 4;
        info->numberOfNodesToReclaim = 3; // reclaim p, s, l (reclamation starts at nodes[1])
        info->numberOfNodesToFreeze = 3;
        info->numberOfNodesAllocated = 1;
        info->type = SCXRecord<K,V>::TYPE_DEL;
        info->nodes[0] = gp;
        info->nodes[1] = p;
        info->nodes[2] = s;
        info->nodes[3] = l;
        bool retval = scx(tid, info, (p == gpleft ? &gp->left : &gp->right), GET_ALLOCATED_NODE_PTR(tid, 0));
        if (retval) {
//            counters->updateChange[info->path]->inc(tid);
        }
        return retval;
    }
}

// this internal function is called only by scx(), and only when otherSCX is protected by a call to shmem->protect
template<class K, class V, class Compare, class RecManager>
inline bool bst<K,V,Compare,RecManager>::tryRetireSCXRecord(const int tid, SCXRecord<K,V> * const otherSCX, Node<K,V> * const node) {
    if (otherSCX == dummy) return false; // never retire the dummy scx record!
    if (IS_VERSION_NUMBER(otherSCX)) return false; // can't retire version numbers!
    if (otherSCX->state.load(memory_order_relaxed) == SCXRecord<K,V>::STATE_COMMITTED) {
        // in this tree, committed scx records are only pointed to by one node.
        // so, when this function is called, the scx record is already retired.
        recmgr->retire(tid, otherSCX);
        return true;
    } else { // assert: scx->state >= STATE_ABORTED
        const int state_aborted = SCXRecord<K,V>::STATE_ABORTED;
        assert(otherSCX->state.load(memory_order_relaxed) >= state_aborted); /* state is aborted */
        // node->scxRecord no longer points to scx, so we set
        // the corresponding bit in scx->state to 0.
        // when state == ABORT_STATE_NO_FLAGS(state), scx is retired.
        const int n = otherSCX->numberOfNodesToFreeze;
        Node<K,V> ** const otherNodes = otherSCX->nodes;
        bool casSucceeded = false;
        int stateNew = -1;
        for (int i=0;i<n;++i) {
            if (otherNodes[i] == node) {
                while (!casSucceeded) {
                    TRACE COUTATOMICTID("attempting state CAS..."<<endl);
                    int stateOld = otherSCX->state.load(memory_order_relaxed);
                    stateNew = STATE_GET_WITH_FLAG_OFF(stateOld, i);
                    DEBUG assert(stateOld >= state_aborted);
                    DEBUG assert(stateNew >= state_aborted);
                    assert(stateNew < stateOld);
                    casSucceeded = otherSCX->state.compare_exchange_weak(stateOld, stateNew);       // MEMBAR ON X86/64 // and on power, because of seq_cst semantics?
                }
                break;
            }
        }
        // many scxs can all be CASing state and trying to retire this node.
        // the one who gets to invoke retire() is the one whose CAS sets
        // the flag subfield of scx->state to 0.
        if (casSucceeded && STATE_GET_FLAGS(stateNew) == 0) {
            recmgr->retire(tid, otherSCX);
            return true;
        }
    }
    return false;
}

// you may call this only in a quiescent state.
// the scx records in scxRecordsSeen must be protected (or we must know no one can have freed them--this is the case in this implementation).
// if this is being called from crash recovery, all nodes in nodes[] and the scx record must be Qprotected.
template<class K, class V, class Compare, class RecManager>
void bst<K,V,Compare,RecManager>::reclaimMemoryAfterSCX(
            const int tid,
            ReclamationInfo<K,V> * info) {
        
    Node<K,V> ** const nodes = info->nodes;
    SCXRecord<K,V> * const * const scxRecordsSeen = (SCXRecord<K,V> * const * const) info->llxResults;
    const int state = info->state;
    const int operationType = info->type;
    
    // NOW, WE ATTEMPT TO RECLAIM ANY RETIRED NODES
    // first, we determine how far we got in the loop in help()
    int highestIndexReached = (state == SCXRecord<K,V>::STATE_COMMITTED 
            ? info->numberOfNodesToFreeze
            : STATE_GET_HIGHEST_INDEX_REACHED(state));
    const int maxNodes = MAX_NODES;
    assert(highestIndexReached>=0);
    assert(highestIndexReached<=maxNodes);
    
    SCXRecord<K,V> *debugSCXRecord = GET_ALLOCATED_SCXRECORD_PTR(tid);
    
    const int state_aborted = SCXRecord<K,V>::STATE_ABORTED;
    const int state_inprogress = SCXRecord<K,V>::STATE_INPROGRESS;
    const int state_committed = SCXRecord<K,V>::STATE_COMMITTED;
    if (highestIndexReached == 0) {
        /* aborted but only got to help() loop iteration 0 */
        assert(state == state_aborted || state == state_inprogress);
        // scx was never inserted into the data structure,
        // so we can reuse it for our next operation.
        return;
    } else {
        assert(highestIndexReached > 0);
        // For DEBRA+: assuming we're in a quiescent state,
        // it's safe to perform non-restartable operations on bookkeeping data structures
        // (since no other thread will force us to restart in a quiescent state).

        // we wrote a pointer to newscxrecord into the data structure,
        // so we cannot reuse it immediately for our next operation.
        // instead, we allocate a new scx record for our next operation.
        assert(!recmgr->supportsCrashRecovery() || recmgr->isQuiescent(tid));
        REPLACE_ALLOCATED_SCXRECORD(tid);

        // the scx records in scxRecordsSeen[] may now be retired
        // (since this scx changed each nodes[i]->scxRecord so that it does not
        //  point to any scx record in scxRecordsSeen[].)
        // we start at j=1 because nodes[0] may have been retired and freed
        // since we entered a quiescent state.
        // furthermore, we don't need to check if nodes[0]->left == NULL, since
        // we know nodes[0] is never a leaf.
//        cout<<"trying to retire scx records"<<endl;
        for (int j=0;j<highestIndexReached;++j) {
            // if nodes[j] is not a leaf, then we froze it, changing the scx record
            // that nodes[j] points to. so, we try to retire the scx record is
            // no longer pointed to by nodes[j].
            // note: we know scxRecordsSeen[j] is not retired, since we have not
            //       zeroed out its flag representing an incoming pointer
            //       from nodes[j] until we execute tryRetireSCXRecord() below.
            //       (it follows that we don't need to invoke protect().)
            if (scxRecordsSeen[j] != LLX_RETURN_IS_LEAF && !IS_VERSION_NUMBER(scxRecordsSeen[j])) {
                bool success = tryRetireSCXRecord(tid, scxRecordsSeen[j], nodes[j]);
            }
        }
        
        SOFTWARE_BARRIER; // prevent compiler from moving retire() calls before tryRetireSCXRecord() calls above

        // if the state was COMMITTED, then we cannot reuse the nodes the we
        // took from allocatedNodes[], either, so we must replace these nodes.
        // for the chromatic tree, the number of nodes can be found in
        // NUM_INSERTS[operationType].
        // in general, we have to add a parameter, specified when you call SCX,
        // that says how large the replacement subtree of new nodes is.
        // alternatively, we could just move this out into the data structure code,
        // to be performed AFTER an scx completes.
        if (state == SCXRecord<K,V>::STATE_COMMITTED) {
//            cout<<"replacing allocated nodes"<<endl;
            for (int i=0;i<info->numberOfNodesAllocated;++i) {
                REPLACE_ALLOCATED_NODE(tid, i);
            }
            // nodes[1], nodes[2], ..., nodes[nNodes-1] are now retired
            for (int j=0;j<info->numberOfNodesToReclaim;++j) {
//                cout<<"retiring nodes["<<(1+j)<<"]"<<endl;
                recmgr->retire(tid, nodes[1+j]);
            }
        } else {
            assert(state >= state_aborted); /* is ABORTED */
        }
    }
}

// you may call this only if each node in nodes is protected by a call to recmgr->protect
template<class K, class V, class Compare, class RecManager>
bool bst<K,V,Compare,RecManager>::scx(
            const int tid,
            ReclamationInfo<K,V> * const info,
            atomic_uintptr_t *field,        // pointer to a "field pointer" that will be changed
            Node<K,V> *newNode) {
    TRACE COUTATOMICTID("scx(tid="<<tid<<" type="<<info->type<<")"<<endl);

    SCXRecord<K,V> *newscxrecord = GET_ALLOCATED_SCXRECORD_PTR(tid);
    initializeSCXRecord(tid, newscxrecord, info, field, newNode);
    
    SOFTWARE_BARRIER;
    int state = help(tid, newscxrecord, false);
    info->state = newscxrecord->state.load(memory_order_relaxed);
    reclaimMemoryAfterSCX(tid, info);
    return state & SCXRecord<K,V>::STATE_COMMITTED;
}

// you may call this only if scx is protected by a call to recmgr->protect.
// each node in scx->nodes must be protected by a call to recmgr->protect.
// returns the state field of the scx record "scx."
template<class K, class V, class Compare, class RecManager>
int bst<K,V,Compare,RecManager>::help(const int tid, SCXRecord<K,V> *scx, bool helpingOther) {
    assert(recmgr->isProtected(tid, scx));
    assert(scx != dummy);
    const int nNodes                        = scx->numberOfNodes;
    const int nFreeze                       = scx->numberOfNodesToFreeze;
    Node<K,V> ** const nodes                = scx->nodes;
    SCXRecord<K,V> ** const scxRecordsSeen  = scx->scxRecordsSeen;
    Node<K,V> * const newNode               = scx->newNode;
    TRACE COUTATOMICTID("help(tid="<<tid<<" scx="<<*scx<<" helpingOther="<<helpingOther<<"), nFreeze="<<nFreeze<<endl);
    LWSYNC;
    int __state = scx->state.load(memory_order_relaxed);
    if (__state != SCXRecord<K,V>::STATE_INPROGRESS) { // TODO: optimize by taking this out, somehow?
        //assert(helpingOther);
        // if state is not in progress here, then helpingOther == true,
        // which means we are helping someone.
        // CONSEQUENTLY, THE RETURN VALUE OF HELP IS IGNORED!
        // !!!!!!!!!!!!!!!!!!!!!!!!!! NO !!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // this changed when crash recovery was added, because now you can
        // help yourself after being suspected of crashing.
        // in this case, the return value of this function is NOT ignored.
        TRACE COUTATOMICTID("help return 0 after state != in progress"<<endl);
        //return 0; // return anything since this will be ignored.
        return __state;
    }
    // note: the above cannot cause us to leak the memory allocated for scx,
    // since, if !helpingOther, then we created the SCX record,
    // and did not write it into the data structure.
    // so, no one could have helped us, and state must be INPROGRESS.
    
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
    int flags = 1; // bit i is 1 if nodes[i] is frozen and not a leaf, and 0 otherwise.
    // note that flags bit 0 is always set, since nodes[0] is never a leaf.
    // (technically, if we abort in the first iteration,
    //  flags=1 makes no sense (since it suggests there is one pointer to scx
    //  from a node in the tree), but in this case we ignore the flags variable.)
    for (int i=helpingOther; i<nFreeze; ++i) {
        if (scxRecordsSeen[i] == LLX_RETURN_IS_LEAF) {
            TRACE COUTATOMICTID("nodes["<<i<<"] is a leaf");
            assert(i > 0); // nodes[0] cannot be a leaf...
            continue; // do not freeze leaves
        }
        
        uintptr_t exp = (uintptr_t) scxRecordsSeen[i];
        bool successfulCAS = nodes[i]->scxRecord.compare_exchange_strong(exp, (uintptr_t) scx);     // MEMBAR ON X86/64 // and on power, since seq_cst semantics?
        
        if (!successfulCAS && (SCXRecord<K,V> *) exp != scx) { // if work was not done
            if (scx->allFrozen.load(memory_order_relaxed)) {
                assert(scx->state.load(memory_order_relaxed) == 1); /*STATE_COMMITTED*/
                TRACE COUTATOMICTID("help return COMMITTED after failed freezing cas on nodes["<<i<<"]"<<endl);
                return SCXRecord<K,V>::STATE_COMMITTED; // success
            } else {
                if (i == 0) {
                    // if i == 0, then our scx record was never in the tree, and,
                    // consequently, no one else can have a pointer to it.
                    // so, there is no need to change scx->state.
                    // (recall that helpers start with helpingOther == true,
                    //  so i>0 for every helper. thus, if and only if i==0,
                    //  we created this scx record and failed our first CAS.)
                    assert(!helpingOther);
                    TRACE COUTATOMICTID("help return ABORTED after failed freezing cas on nodes["<<i<<"]"<<endl);
                    scx->state.store(ABORT_STATE_INIT(0, 0), memory_order_relaxed);
                    return ABORT_STATE_INIT(0, 0); // scx is aborted (but no one else will ever know)
                } else {
                    // if this is the first failed freezing CAS to occur for this SCX,
                    // then flags encodes the pointers to this scx record from nodes IN the tree.
                    // (the following CAS will succeed only the first time it is performed
                    //  by any thread running help() for this scx.)
                    int expectedState = SCXRecord<K,V>::STATE_INPROGRESS;
                    int newState = ABORT_STATE_INIT(i, flags);
                    bool success = scx->state.compare_exchange_strong(expectedState, newState);     // MEMBAR ON X86/64
                    assert(expectedState != 1); /* not committed */
                    // note2: a regular write will not do, here, since two people can start helping, one can abort at i>0, then after a long time, the other can fail to CAS i=0, so they can get different i values.
                    assert(scx->state & 2); /* SCXRecord<K,V>::STATE_ABORTED */
                    // ABORTED THE SCX AFTER PERFORMING ONE OR MORE SUCCESSFUL FREEZING CASs
                    if (success) {
                        TRACE COUTATOMICTID("help return ABORTED(changed to "<<newState<<") after failed freezing cas on nodes["<<i<<"]"<<endl);
                        return newState;
                    } else {
                        TRACE COUTATOMICTID("help return ABORTED(failed to change to "<<newState<<" because encountered "<<expectedState<<" instead of in progress) after failed freezing cas on nodes["<<i<<"]"<<endl);
                        return expectedState; // this has been overwritten by compare_exchange_strong with the value that caused the CAS to fail.
                    }
                }
            }
        } else {
            flags |= (1<<i); // nodes[i] was frozen for scx
            const int state_inprogress = SCXRecord<K,V>::STATE_INPROGRESS;
            assert((SCXRecord<K,V> *) exp == scx || IS_VERSION_NUMBER(exp) || (((SCXRecord<K,V> *) exp)->state.load(memory_order_relaxed) != state_inprogress));
        }
    }
    //LWSYNC; // not needed, since last step in prev loop is a successful cas, which implies a membar
    scx->allFrozen.store(true, memory_order_relaxed);
    // note: i think the sequential consistency memory model is not actually needed here...
    // why? in an execution where no reads are moved before allFrozen by the
    // compiler/cpu (because we added a barrier here), any process that sees
    // allFrozen = true has also just seen that nodes[i]->op != &op,
    // which means that the operation it is helping has already completed!
    // in particular, the child CAS will already have been done, which implies
    // that allFrozen will have been set to true, since the compiler/cpu cannot
    // move the (first) child CAS before the (first) write to allFrozen.
    SOFTWARE_BARRIER;
    LWSYNC;
    for (int i=1; i<nFreeze; ++i) {
        if (scxRecordsSeen[i] == LLX_RETURN_IS_LEAF) continue; // do not mark leaves
        nodes[i]->marked.store(true, memory_order_relaxed); // finalize all but first node
    }
    LWSYNC;
    // CAS in the new sub-tree (update CAS)
    uintptr_t expected = (uintptr_t) nodes[1];
    scx->field->compare_exchange_strong(expected, (uintptr_t) newNode);                             // MEMBAR ON X86/64
    assert(scx->state.load(memory_order_relaxed) < 2); // not aborted
    scx->state.store(SCXRecord<K,V>::STATE_COMMITTED, memory_order_relaxed);
    
    TRACE COUTATOMICTID("help return COMMITTED after performing update cas"<<endl);
    return SCXRecord<K,V>::STATE_COMMITTED; // success
}

// you may call this only if node is protected by a call to recmgr->protect
template<class K, class V, class Compare, class RecManager>
void *bst<K,V,Compare,RecManager>::llx(
            const int tid,
            Node<K,V> *node,
            Node<K,V> **retLeft,
            Node<K,V> **retRight) {
    TRACE COUTATOMICTID("llx(tid="<<tid<<", node="<<*node<<")"<<endl);
    bst_retired_info info;
    SCXRecord<K,V> *scx1 = (SCXRecord<K,V>*) node->scxRecord.load(memory_order_relaxed);
    int state = (IS_VERSION_NUMBER(scx1) ? SCXRecord<K,V>::STATE_COMMITTED : scx1->state.load(memory_order_relaxed));
    LWSYNC;
    SOFTWARE_BARRIER;       // prevent compiler from moving the read of marked before the read of state (no hw barrier needed on x86/64, since there is no read-read reordering)
    bool marked = node->marked.load(memory_order_relaxed);
    SOFTWARE_BARRIER;       // prevent compiler from moving the reads scx2=node->scxRecord or scx3=node->scxRecord before the read of marked. (no h/w barrier needed on x86/64 since there is no read-read reordering)
    if ((state & SCXRecord<K,V>::STATE_COMMITTED && !marked) || state & SCXRecord<K,V>::STATE_ABORTED) {
        SOFTWARE_BARRIER;       // prevent compiler from moving the reads scx2=node->scxRecord or scx3=node->scxRecord before the read of marked. (no h/w barrier needed on x86/64 since there is no read-read reordering)
        *retLeft = (Node<K,V>*) node->left.load(memory_order_relaxed);
        *retRight = (Node<K,V>*) node->right.load(memory_order_relaxed);
        if (*retLeft == NULL) {
            TRACE COUTATOMICTID("llx return2.a (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<")\n"); 
            return (void *) LLX_RETURN_IS_LEAF;
        }
        SOFTWARE_BARRIER; // prevent compiler from moving the read of node->scxRecord before the read of left or right
        LWSYNC;
        SCXRecord<K,V> *scx2 = (SCXRecord<K,V>*) node->scxRecord.load(memory_order_relaxed);
        if (scx1 == scx2) {
            TRACE COUTATOMICTID("llx return2 (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<" scx1="<<scx1<<")\n"); 
            DEBUG counters->llxSuccess->inc(tid);
            // on x86/64, we do not need any memory barrier here to prevent mutable fields of node from being moved before our read of scx1, because the hardware does not perform read-read reordering. on another platform, we would need to ensure no read from after this point is reordered before this point (technically, before the read that becomes scx1)...
            return scx1;    // success
        } else {
            if (recmgr->shouldHelp()) {
                TRACE COUTATOMICTID("llx help 1 tid="<<tid<<endl);
                if (!IS_VERSION_NUMBER(scx2)) {
                    help(tid, scx2, true);
                }
            }
        }
    } else if (state == SCXRecord<K,V>::STATE_INPROGRESS) {
        if (recmgr->shouldHelp()) {
            assert(scx1 != dummy);
            assert(recmgr->isProtected(tid, scx1));
            TRACE COUTATOMICTID("llx help 2 tid="<<tid<<endl);
            if (!IS_VERSION_NUMBER(scx1)) {
                help(tid, scx1, true);
            }
        }
    } else {
        // state committed and marked
        assert(state == 1); /* SCXRecord<K,V>::STATE_COMMITTED */
        assert(marked);
        if (recmgr->shouldHelp()) {
            LWSYNC;
            SCXRecord<K,V> *scx3 = (SCXRecord<K,V>*) node->scxRecord.load(memory_order_relaxed);
            TRACE COUTATOMICTID("llx help 3 tid="<<tid<<endl);
            if (!IS_VERSION_NUMBER(scx3)) {
                help(tid, scx3, true);
            }
        } else {
        }
    }
    TRACE COUTATOMICTID("llx return5 (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<")\n");
    DEBUG counters->llxFail->inc(tid);
    return NULL;            // fail
}
