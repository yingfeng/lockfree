/**
 * Preliminary C++ implementation of unbalanced binary search tree using LLX/SCX.
 *
 * Copyright (C) 2014 Trevor Brown
 * 
 * Why is this code so long?
 * - Because this file defines FOUR implementations
 *   (1) transactional lock elision (suffix _tle)
 *   (2) hybrid tm based implementation (suffix _tm)
 *   (3) 3-path implementation (suffixes _fallback, _middle, _fast)
 *   (4) global locking (suffix _lock_search_inplace)
 * - Because the LLX and SCX synchronization primitives are implemented here
 *   (including memory reclamation for SCX records)
 */

#include "bst.h"
#include <cassert>
#include <cstdlib>
#include "../globals_extern.h"
#include "../globals.h"
using namespace std;

#ifdef TM
#include <setjmp.h>
__thread sigjmp_buf ___jbuf;
#endif

//extern void *singleton;
//extern pthread_key_t pthreadkey;
//extern volatile long interruptible[MAX_TID_POW2*PREFETCH_SIZE_WORDS];
//#ifdef POSIX_SYSTEM
//void crashhandler(int signum, siginfo_t *info, void *uctx);
//#endif

#ifdef NOREBALANCING
#define IFREBALANCING if (0)
#else
#define IFREBALANCING if (1)
#endif

#ifdef NO_TXNS
    #define XBEGIN() _XBEGIN_STARTED
    #define XEND() ;
    #define XABORT(_status) { status = (_status); goto aborthere; }
#endif

#ifdef POSIX_SYSTEM
#ifdef CRASH_RECOVERY_USING_SETJMP
#define UPDATE_BLOCK_WITHRECOVERY_BEGIN(tid, finishedbool) \
    if (RecManager::supportsCrashRecovery() && sigsetjmp(shmem->setjmpbuffers[(tid)], 0)) { \
        /*if (shmem->supportsCrashRecovery()) { \
            interruptible[tid*PREFETCH_SIZE_WORDS] = false; \
            __sync_synchronize(); \
        }*/ \
        shmem->enterQuiescentState((tid)); \
        /*blockCrashRecoverySignal();*/ \
        (finishedbool) = recoverAnyAttemptedSCX((tid), -1); \
        /*COUTATOMICTID("thread "<<(tid)<<" caught suspected crash exception"<<endl);*/ \
        unblockCrashRecoverySignal(); \
    } else
#define UPDATE_BLOCK_NORECOVERY_BEGIN(tid) \
    if (RecManager::supportsCrashRecovery() && sigsetjmp(shmem->setjmpbuffers[(tid)], 0)) { \
        /*if (shmem->supportsCrashRecovery()) { \
            interruptible[tid*PREFETCH_SIZE_WORDS] = false; \
            __sync_synchronize(); \
        }*/ \
        shmem->enterQuiescentState((tid)); \
        /*blockCrashRecoverySignal();*/ \
        /*COUTATOMICTID("thread "<<(tid)<<" caught suspected crash exception"<<endl);*/ \
        /*COUTATOMICTID("exiting for debug purposes..."<<endl);*/ \
        /*exit(-1);*/ \
        unblockCrashRecoverySignal(); \
    } else
#define UPDATE_BLOCK_WITHRECOVERY_END(tid, finishedbool)
#define UPDATE_BLOCK_NORECOVERY_END(tid)
#endif
#else
#define UPDATE_BLOCK_WITHRECOVERY_BEGIN(tid, finished) if(1)
#define UPDATE_BLOCK_NORECOVERY_BEGIN(tid) if(1)
#define UPDATE_BLOCK_WITHRECOVERY_END(tid, finished)
#define UPDATE_BLOCK_NORECOVERY_END(tid)
#endif

#define IF_FAIL_TO_PROTECT_SCX(info, tid, _obj, arg2, arg3) \
    info.obj = _obj; \
    info.ptrToObj = (void * volatile *) arg2; \
    info.nodeContainingPtrToObjIsMarked = arg3; /*bst_retired_info(obj, arg2, arg3);*/ \
    if (_obj != dummy && !shmem->protect(tid, _obj, callbackCheckNotRetired, (void*) &info))
#define IF_FAIL_TO_PROTECT_NODE(info, tid, _obj, arg2, arg3) \
    info.obj = _obj; \
    info.ptrToObj = (void * volatile *) arg2; \
    info.nodeContainingPtrToObjIsMarked = arg3; /*bst_retired_info(obj, arg2, arg3);*/ \
    if (_obj != root && !shmem->protect(tid, _obj, callbackCheckNotRetired, (void*) &info))

//__rtm_force_inline CallbackReturn callbackReturnTrue(CallbackArg arg) {
//    return true;
//}

__rtm_force_inline CallbackReturn callbackCheckNotRetired(CallbackArg arg) {
    bst_retired_info *info = (bst_retired_info*) arg;
    if (*info->ptrToObj == info->obj) {
        // we insert a compiler barrier (not a memory barrier!)
        // to prevent these if statements from being merged or reordered.
        // we care because we need to see that ptrToObj == obj
        // and THEN see that ptrToObject is a field of an object
        // that is not marked. seeing both of these things,
        // in this order, implies that obj is in the data structure.
        SOFTWARE_BARRIER;
        if (!*info->nodeContainingPtrToObjIsMarked) {
            return true;
        }
    }
    return false;
}

template<class K, class V, class Compare, class RecManager>
__rtm_force_inline SCXRecord<K,V>* bst<K,V,Compare,RecManager>::allocateSCXRecord(
            const int tid) {
    SCXRecord<K,V> *newop = shmem->template allocate<SCXRecord<K,V> >(tid);
    if (newop == NULL) {
        COUTATOMICTID("ERROR: could not allocate scx record"<<endl);
        exit(-1);
    }
    return newop;
}

template<class K, class V, class Compare, class RecManager>
__rtm_force_inline SCXRecord<K,V>* bst<K,V,Compare,RecManager>::initializeSCXRecord(
            const int tid,
            SCXRecord<K,V> * const newop,
            ReclamationInfo<K,V> * const info,
            Node<K,V> * volatile * const field,
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
    newop->state = SCXRecord<K,V>::STATE_INPROGRESS;
    newop->allFrozen = false;
    newop->field = field;
    newop->numberOfNodes = (char) info->numberOfNodes;
    newop->numberOfNodesToFreeze = (char) info->numberOfNodesToFreeze;
    return newop;
}

template<class K, class V, class Compare, class RecManager>
__rtm_force_inline Node<K,V>* bst<K,V,Compare,RecManager>::allocateNode(
            const int tid) {
    Node<K,V> *newnode = shmem->template allocate<Node<K,V> >(tid);
    if (newnode == NULL) {
        COUTATOMICTID("ERROR: could not allocate node"<<endl);
        exit(-1);
    }
    return newnode;
}

template<class K, class V, class Compare, class RecManager>
__rtm_force_inline Node<K,V>* bst<K,V,Compare,RecManager>::initializeNode(
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
    newnode->left = left;
    newnode->right = right;
    newnode->scxRecord = dummy;
    newnode->marked = false;
    return newnode;
}

template<class K, class V, class Compare, class RecManager>
void bst<K,V,Compare,RecManager>::unblockCrashRecoverySignal() {
#ifdef POSIX_SYSTEM
    __sync_synchronize();
    sigset_t oldset;
//    int x = pthread_sigmask(0, NULL, &oldset);
//    if (sigismember(&oldset, this->shmem->suspectedCrashSignal)) {
//        VERBOSE COUTATOMICTID("BLOCKED SIGNALS FOR THREAD INCLUDE THE SUSPECTED CRASH SIGNAL."<<endl);
        sigemptyset(&oldset);
        sigaddset(&oldset, this->shmem->suspectedCrashSignal);
        if (pthread_sigmask(SIG_UNBLOCK, &oldset, NULL)) {
            VERBOSE COUTATOMIC("ERROR UNBLOCKING SIGNAL"<<endl);
            exit(-1);
        } //else {
//            VERBOSE COUTATOMIC("Successfully unblocked signal"<<endl);
//        }
//    }
#endif
}

template<class K, class V, class Compare, class RecManager>
void bst<K,V,Compare,RecManager>::blockCrashRecoverySignal() {
#ifdef POSIX_SYSTEM
    __sync_synchronize();
    sigset_t oldset;
    sigemptyset(&oldset);
    sigaddset(&oldset, this->shmem->suspectedCrashSignal);
    if (pthread_sigmask(SIG_BLOCK, &oldset, NULL)) {
        VERBOSE COUTATOMIC("ERROR BLOCKING SIGNAL"<<endl);
        exit(-1);
    }
#endif
}

template<class K, class V, class Compare, class RecManager>
long long bst<K,V,Compare,RecManager>::debugKeySum(Node<K,V> * node) {
    if (node == NULL) return 0;
    if (node->left == NULL) return (long long) node->key;
    return debugKeySum(node->left)
         + debugKeySum(node->right);
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
    return computeSize(root->left->left);
}
    
template<class K, class V, class Compare, class RecManager>
inline int bst<K,V,Compare,RecManager>::computeSize(Node<K,V> * const root) {
    if (root == NULL) return 0;
    if (root->left != NULL) { // if internal node
        return computeSize(root->left)
                + computeSize(root->right);
    } else { // if leaf
        return 1;
//        printf(" %d", root->key);
    }
}

//template<class K, class V, class Compare, class RecManager>
//bool bst<K,V,Compare,RecManager>::contains(const int tid, const K& key) {
//    pair<V,bool> result = find(tid, key);
//    return result.second;
//}

template<class K, class V, class Compare, class RecManager>
int bst<K,V,Compare,RecManager>::rangeQuery(const int tid, const K& low, const K& hi, Node<K,V> const ** result) {
    int cnt;
    void *input[] = {(void*) &low, (void*) &hi};
    void *output[] = {(void*) result, (void*) &cnt};
    htmWrapper(CAST_UPDATE_FUNCTION(rangeQuery_txn),
               CAST_UPDATE_FUNCTION(RQNAME),
               CAST_UPDATE_FUNCTION(RQNAME), tid, input, output);
    return cnt;
}

template<class K, class V, class Compare, class RecManager>
int bst<K,V,Compare,RecManager>::rangeQuery_tle(const int tid, const K& low, const K& hi, Node<K,V> const ** result) {
    int cnt;

    block<Node<K,V> > stack (NULL);

    shmem->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, counters->pathSuccess, counters->pathFail, counters->htmAbort);

    cnt = 0;
    
    // depth first traversal (of interesting subtrees)
    stack.push(root);
    while (!stack.isEmpty()) {
        Node<K,V> * node = stack.pop();
        Node<K,V> * left = node->left;

        // if internal node, explore its children
        if (left != NULL) {
            if (node->key != this->NO_KEY && !cmp(hi, node->key)) {
                Node<K,V> *right = node->right;
                stack.push(right);
            }
            if (node->key == this->NO_KEY || cmp(low, node->key)) {
                stack.push(left);
            }

        // else if leaf node, check if we should return it
        } else {
            //visitedNodes[cnt] = node;
            if (node->key != this->NO_KEY && !cmp(node->key, low) && !cmp(hi, node->key)) {
                result[cnt++] = node;
            }
        }
    }
    
    scope.end();
    shmem->enterQuiescentState(tid);
    return cnt;
}

#ifdef TM
template<class K, class V, class Compare, class RecManager>
int bst<K,V,Compare,RecManager>::rangeQuery_tm(TM_ARGDECL_ALONE, const int tid, const K& low, const K& hi, Node<K,V> const ** result) {
    int cnt;

    shmem->leaveQuiescentState(tid);
    TM_BEGIN_RO(___jbuf);

//    Node<K,V> * stack[256];
//    int sz = 0;
//    stack[sz++] = (Node<K,V> *) TM_SHARED_READ_P(root);
//    cnt = 0;
//    
//    // depth first traversal (of interesting subtrees)
//    while (sz > 0) {
//        Node<K,V> * node = stack[--sz];
//        Node<K,V> * left = (Node<K,V> *) TM_SHARED_READ_P(node->left);
//
//        // if internal node, explore its children
//        if (left != NULL) {
//            K nkey = TM_SHARED_READ_L(node->key);
//            if (nkey != this->NO_KEY && !cmp(hi, nkey)) {
//                Node<K,V> *right = (Node<K,V> *) TM_SHARED_READ_P(node->right);
//                if (sz < 256) {
//                    stack[sz++] = right;
//                } else {
//                    exit(-1);
//                }
//            }
//            if (nkey == this->NO_KEY || cmp(low, nkey)) {
//                if (sz < 256) {
//                    stack[sz++] = left;
//                } else {
//                    exit(-1);
//                }
//            }
//
//        // else if leaf node, check if we should return it
//        } else {
//            //visitedNodes[cnt] = node;
//            K nkey = TM_SHARED_READ_L(node->key);
//            if (nkey != this->NO_KEY && !cmp(nkey, low) && !cmp(hi, nkey)) {
//                result[cnt++] = node;
//            }
//        }
//    }
    
    block<Node<K,V> > stack (NULL);
    cnt = 0;
    
    // depth first traversal (of interesting subtrees)
    stack.push((Node<K,V> *) TM_SHARED_READ_P(root));
    while (!stack.isEmpty()) {
        Node<K,V> * node = stack.pop();
        Node<K,V> * left = (Node<K,V> *) TM_SHARED_READ_P(node->left);

        // if internal node, explore its children
        if (left != NULL) {
            K nkey = TM_SHARED_READ_L(node->key);
            if (nkey != this->NO_KEY && !cmp(hi, nkey)) {
                Node<K,V> *right = (Node<K,V> *) TM_SHARED_READ_P(node->right);
                stack.push(right);
            }
            if (nkey == this->NO_KEY || cmp(low, nkey)) {
                stack.push(left);
            }

        // else if leaf node, check if we should return it
        } else {
            //visitedNodes[cnt] = node;
            K nkey = TM_SHARED_READ_L(node->key);
            if (nkey != this->NO_KEY && !cmp(nkey, low) && !cmp(hi, nkey)) {
                result[cnt++] = node;
            }
        }
    }
    
    TM_END();
    shmem->enterQuiescentState(tid);
    return cnt;
}
#endif

template<class K, class V, class Compare, class RecManager>
int bst<K,V,Compare,RecManager>::rangeQuery_txn(ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) {
    //COUTATOMICTID("rangeQuery_lock(low="<<low<<", hi="<<hi<<")"<<endl);
    Node<K,V> const ** result = (Node<K,V> const **) output[0];
    int *cnt = (int*) output[1];
    const K& low = *((const K*) input[0]);
    const K& hi = *((const K*) input[1]);

    block<Node<K,V> > stack (NULL);
    *cnt = 0;
    
    int attempts = MAX_FAST_HTM_RETRIES;
TXN1: (0);
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        // depth first traversal (of interesting subtrees)
        stack.push(root);
        while (!stack.isEmpty()) {
            Node<K,V> * node = stack.pop();
            //COUTATOMICTID("    visiting node "<<*node<<endl);
            Node<K,V> * left = node->left;

            // if internal node, explore its children
            if (left != NULL) {
                //COUTATOMICTID("    internal node key="<<node->key<<" low="<<low<<" hi="<<hi<<" cmp(hi, node->key)="<<cmp(hi, node->key)<<" cmp(low, node->key)="<<cmp(low, node->key)<<endl);
                if (node->key != this->NO_KEY && !cmp(hi, node->key)) {
                    //COUTATOMICTID("    stack.push right: "<<right<<endl);
                    Node<K,V> *right = node->right;
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
        XEND();
    } else {
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        if (status & _XABORT_CAPACITY) info->capacityAborted[info->path] = true;
        info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        return false;
    }
    // success
//    if (info->capacityAborted[info->path]) this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid);
    return true;
}

template<class K, class V, class Compare, class RecManager>
int bst<K,V,Compare,RecManager>::rangeQuery_lock(ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) {
    //COUTATOMICTID("rangeQuery_lock(low="<<low<<", hi="<<hi<<")"<<endl);
    Node<K,V> const ** result = (Node<K,V> const **) output[0];
    int *cnt = (int*) output[1];
    const K& low = *((const K*) input[0]);
    const K& hi = *((const K*) input[1]);

    block<Node<K,V> > stack (NULL);
    *cnt = 0;
    
    // acquire lock on the fallback path
    acquireLock(&this->lock);

    // depth first traversal (of interesting subtrees)
    stack.push(root);
    while (!stack.isEmpty()) {
        Node<K,V> * node = stack.pop();
        //COUTATOMICTID("    visiting node "<<*node<<endl);
        Node<K,V> * left = node->left;
        
        // if internal node, explore its children
        if (left != NULL) {
            //COUTATOMICTID("    internal node key="<<node->key<<" low="<<low<<" hi="<<hi<<" cmp(hi, node->key)="<<cmp(hi, node->key)<<" cmp(low, node->key)="<<cmp(low, node->key)<<endl);
            if (node->key != this->NO_KEY && !cmp(hi, node->key)) {
                //COUTATOMICTID("    stack.push right: "<<right<<endl);
                Node<K,V> *right = node->right;
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
    
    // let other operations know that we are finished
    releaseLock(&this->lock);

    // success
    return true;
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
        if (result[i]->marked) {
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
//        UPDATE_BLOCK_NORECOVERY_BEGIN(tid) {
            shmem->leaveQuiescentState(tid);
            // root is never retired, so we don't need to call
            // protectPointer before accessing its child pointers
            p = root->left;
            IF_FAIL_TO_PROTECT_NODE(info, tid, p, &root->left, &root->marked) {
                shmem->enterQuiescentState(tid); 
                continue; /* retry */ 
            }
            assert(p != root);
            assert(shmem->isProtected(tid, p));
            l = p->left;
            if (l == NULL) {
                result = pair<V,bool>(NO_VALUE, false); // no keys in data structure
                shmem->enterQuiescentState(tid);
                return result; // success
            }
            IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) { 
                shmem->enterQuiescentState(tid); 
                continue; /* retry */ 
            }

            assert(shmem->isProtected(tid, l));
            while (l->left != NULL) {
                TRACE COUTATOMICTID("traversing tree; l="<<*l<<endl);
                assert(shmem->isProtected(tid, p));
                shmem->unprotect(tid, p);
                p = l; // note: the new p is currently protected
                assert(shmem->isProtected(tid, p));
                assert(p->key != NO_KEY);
                if (cmp(key, p->key)) {
                    assert(shmem->isProtected(tid, p));
                    l = p->left;
                    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->left, &p->marked) {
                        shmem->enterQuiescentState(tid); 
                        continue; /* retry */ 
                    }
                } else {
                    assert(shmem->isProtected(tid, p));
                    l = p->right;
                    IF_FAIL_TO_PROTECT_NODE(info, tid, l, &p->right, &p->marked) { 
                        shmem->enterQuiescentState(tid); 
                        continue; /* retry */ 
                    }
                }
                assert(shmem->isProtected(tid, l));
            }
            assert(shmem->isProtected(tid, l));
            if (key == l->key) {
                assert(shmem->isProtected(tid, l));
                result = pair<V,bool>(l->value, true);
            } else {
                result = pair<V,bool>(NO_VALUE, false);
            }
            shmem->enterQuiescentState(tid);
            return result; // success
//        } UPDATE_BLOCK_NORECOVERY_END(tid);
    }
    return pair<V,bool>(NO_VALUE, false);
}

template<class K, class V, class Compare, class RecManager>
const pair<V,bool> bst<K,V,Compare,RecManager>::find_tle(const int tid, const K& key) {
    pair<V,bool> result;
    Node<K,V> *p;
    Node<K,V> *l;
    
    shmem->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, counters->pathSuccess, counters->pathFail, counters->htmAbort);

    TRACE COUTATOMICTID("find(tid="<<tid<<" key="<<key<<")"<<endl);
    // root is never retired, so we don't need to call
    // protectPointer before accessing its child pointers
    p = root->left;
    assert(p != root);
    l = p->left;
    if (l == NULL) {
        result = pair<V,bool>(NO_VALUE, false); // no keys in data structure
        
        scope.end();
        shmem->enterQuiescentState(tid);
        return result; // success
    }

    assert(shmem->isProtected(tid, l));
    while (l->left != NULL) {
        TRACE COUTATOMICTID("traversing tree; l="<<*l<<endl);
        p = l; // note: the new p is currently protected
        if (cmp(key, p->key)) {
            l = p->left;
        } else {
            l = p->right;
        }
    }
    if (key == l->key) {
        result = pair<V,bool>(l->value, true);
    } else {
        result = pair<V,bool>(NO_VALUE, false);
    }

    scope.end();
    shmem->enterQuiescentState(tid);
    return result; // success
}

#ifdef TM
template<class K, class V, class Compare, class RecManager>
const pair<V,bool> bst<K,V,Compare,RecManager>::find_tm(TM_ARGDECL_ALONE, const int tid, const K& key) {
    pair<V,bool> result;
    Node<K,V> *p;
    Node<K,V> *l;
    
    shmem->leaveQuiescentState(tid);
    TM_BEGIN_RO(___jbuf);

    TRACE COUTATOMICTID("find(tid="<<tid<<" key="<<key<<")"<<endl);
    // root is never retired, so we don't need to call
    // protectPointer before accessing its child pointers
    p = (Node<K,V> *) TM_SHARED_READ_P(root);
    p = (Node<K,V> *) TM_SHARED_READ_P(p->left);
    l = (Node<K,V> *) TM_SHARED_READ_P(p->left);
    if (l == NULL) {
        result = pair<V,bool>(NO_VALUE, false); // no keys in data structure
        TM_END();
        shmem->enterQuiescentState(tid);
        return result; // success
    }

    while (TM_SHARED_READ_P(l->left) != NULL) {
        TRACE COUTATOMICTID("traversing tree; l="<<*l<<endl);
        p = l; // note: the new p is currently protected
        if (cmp(key, TM_SHARED_READ_L(p->key))) {
            l = (Node<K,V> *) TM_SHARED_READ_P(p->left);
        } else {
            l = (Node<K,V> *) TM_SHARED_READ_P(p->right);
        }
    }
    if (key == TM_SHARED_READ_L(l->key)) {
        result = pair<V,bool>(TM_SHARED_READ_L(l->value), true);
    } else {
        result = pair<V,bool>(NO_VALUE, false);
    }

    TM_END();
    shmem->enterQuiescentState(tid);
    return result; // success
}
#endif

template<class K, class V, class Compare, class RecManager>
const V bst<K,V,Compare,RecManager>::insert(const int tid, const K& key, const V& val) {
    bool onlyIfAbsent = false;
    V result = NO_VALUE;
    void *input[] = {(void*) &key, (void*) &val, (void*) &onlyIfAbsent};
    void *output[] = {(void*) &result};
    htmWrapper(CAST_UPDATE_FUNCTION(UPDATEINSERT_P1),
               CAST_UPDATE_FUNCTION(UPDATEINSERT_P2),
               CAST_UPDATE_FUNCTION(UPDATEINSERT_P3), tid, input, output);
    return result;
}

template<class K, class V, class Compare, class RecManager>
const V bst<K,V,Compare,RecManager>::insert_tle(const int tid, const K& key, const V& val) {
    shmem->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, counters->pathSuccess, counters->pathFail, counters->htmAbort);

    initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, /*1,*/ NULL, NULL);
    Node<K,V> *p = root, *l;
    l = root->left;
    if (l->left != NULL) { // the tree contains some node besides sentinels...
        p = l;
        l = l->left;    // note: l must have key infinity, and l->left must not.
        while (l->left != NULL) {
            p = l;
            if (cmp(key, p->key)) {
                l = p->left;
            } else {
                l = p->right;
            }
        }
    }
    // if we find the key in the tree already
    if (key == l->key) {
        V result = l->value;
        l->value = val;
        scope.end();
        shmem->enterQuiescentState(tid);
        return result;
    } else {
        if (l->key == NO_KEY || cmp(key, l->key)) {
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), l->key, l->value, /*newWeight,*/ GET_ALLOCATED_NODE_PTR(tid, 0), l);
        } else {
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), key, val, /*newWeight,*/ l, GET_ALLOCATED_NODE_PTR(tid, 0));
        }

        Node<K,V> *pleft = p->left;
        if (l == pleft) {
            p->left = GET_ALLOCATED_NODE_PTR(tid, 1);
        } else {
            p->right = GET_ALLOCATED_NODE_PTR(tid, 1);
        }
        scope.end();
        shmem->enterQuiescentState(tid);

        // do memory reclamation and allocation
        REPLACE_ALLOCATED_NODE(tid, 0);
        REPLACE_ALLOCATED_NODE(tid, 1);

        return NO_VALUE;
    }
}

#ifdef TM
template<class K, class V, class Compare, class RecManager>
const V bst<K,V,Compare,RecManager>::insert_tm(TM_ARGDECL_ALONE, const int tid, const K& key, const V& val) {
    shmem->leaveQuiescentState(tid);
    TM_BEGIN(___jbuf);

    initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, /*1,*/ NULL, NULL);
    Node<K,V> *p = (Node<K,V> *) TM_SHARED_READ_P(root);
    Node<K,V> *l = (Node<K,V> *) TM_SHARED_READ_P(p->left);
    if (TM_SHARED_READ_P(l->left) != NULL) { // the tree contains some node besides sentinels...
        p = l;
        l = (Node<K,V> *) TM_SHARED_READ_P(l->left);    // note: l must have key infinity, and l->left must not.
        while (TM_SHARED_READ_P(l->left) != NULL) {
            p = l;
            if (cmp(key, TM_SHARED_READ_L(p->key))) {
                l = (Node<K,V> *) TM_SHARED_READ_P(p->left);
            } else {
                l = (Node<K,V> *) TM_SHARED_READ_P(p->right);
            }
        }
    }
    // if we find the key in the tree already
    if (key == TM_SHARED_READ_L(l->key)) {
        V result = TM_SHARED_READ_L(l->value);
        TM_SHARED_WRITE_L(l->value, val);
        TM_END();
        shmem->enterQuiescentState(tid);
        return result;
    } else {
        if (TM_SHARED_READ_L(l->key) == NO_KEY || cmp(key, TM_SHARED_READ_L(l->key))) {
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), TM_SHARED_READ_L(l->key), TM_SHARED_READ_L(l->value), /*newWeight,*/ GET_ALLOCATED_NODE_PTR(tid, 0), l);
        } else {
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), key, val, /*newWeight,*/ l, GET_ALLOCATED_NODE_PTR(tid, 0));
        }

        Node<K,V> *pleft = (Node<K,V> *) TM_SHARED_READ_P(p->left);
        if (l == pleft) {
            TM_SHARED_WRITE_P(p->left, GET_ALLOCATED_NODE_PTR(tid, 1));
        } else {
            TM_SHARED_WRITE_P(p->right, GET_ALLOCATED_NODE_PTR(tid, 1));
        }
        TM_END();
        shmem->enterQuiescentState(tid);

        // do memory reclamation and allocation
        REPLACE_ALLOCATED_NODE(tid, 0);
        REPLACE_ALLOCATED_NODE(tid, 1);

        return NO_VALUE;
    }
}
#endif

template<class K, class V, class Compare, class RecManager>
const pair<V,bool> bst<K,V,Compare,RecManager>::erase(const int tid, const K& key) {
    V result = NO_VALUE;
    void *input[] = {(void*) &key};
    void *output[] = {(void*) &result};
    htmWrapper(CAST_UPDATE_FUNCTION(UPDATEERASE_P1),
               CAST_UPDATE_FUNCTION(UPDATEERASE_P2),
               CAST_UPDATE_FUNCTION(UPDATEERASE_P3), tid, input, output);
    return pair<V,bool>(result, (result != NO_VALUE));
}

template<class K, class V, class Compare, class RecManager>
const pair<V,bool> bst<K,V,Compare,RecManager>::erase_tle(const int tid, const K& key) {
    shmem->leaveQuiescentState(tid);
    TLEScope scope (&this->lock, MAX_FAST_HTM_RETRIES, tid, counters->pathSuccess, counters->pathFail, counters->htmAbort);

    V result = NO_VALUE;
    
    Node<K,V> *gp, *p, *l;
    l = root->left;
    if (l->left == NULL) {
        scope.end();
        shmem->enterQuiescentState(tid);
        return pair<V,bool>(result, (result != NO_VALUE)); // success
    } // only sentinels in tree...
    gp = root;
    p = l;
    l = p->left;    // note: l must have key infinity, and l->left must not.
    while (l->left != NULL) {
        gp = p;
        p = l;
        if (cmp(key, p->key)) {
            l = p->left;
        } else {
            l = p->right;
        }
    }
    // if we fail to find the key in the tree
    if (key != l->key) {
        scope.end();
        shmem->enterQuiescentState(tid);
    } else {
        Node<K,V> *gpleft, *gpright;
        Node<K,V> *pleft, *pright;
        Node<K,V> *sleft, *sright;
        gpleft = gp->left;
        gpright = gp->right;
        pleft = p->left;
        pright = p->right;
        // assert p is a child of gp, l is a child of p
        Node<K,V> *s = (l == pleft ? pright : pleft);
        sleft = s->left;
        sright = s->right;
        if (p == gpleft) {
            gp->left = s;
        } else {
            gp->right = s;
        }
        result = l->value;
        scope.end();
        shmem->enterQuiescentState(tid);

        // do memory reclamation and allocation
        shmem->retire(tid, p);
        shmem->retire(tid, l);
    }
    return pair<V,bool>(result, (result != NO_VALUE));
}

#ifdef TM
template<class K, class V, class Compare, class RecManager>
const pair<V,bool> bst<K,V,Compare,RecManager>::erase_tm(TM_ARGDECL_ALONE, const int tid, const K& key) {
    shmem->leaveQuiescentState(tid);
    TM_BEGIN(___jbuf);

    V result = NO_VALUE;
    
    Node<K,V> *gp, *p, *l;
    l = (Node<K,V> *) TM_SHARED_READ_P(((Node<K,V> *) TM_SHARED_READ_P(root))->left);
    if (TM_SHARED_READ_P(l->left) == NULL) {
        TM_END();
        shmem->enterQuiescentState(tid);
        return pair<V,bool>(result, (result != NO_VALUE)); // success
    } // only sentinels in tree...
    gp = (Node<K,V> *) TM_SHARED_READ_P(root);
    p = l;
    l = (Node<K,V> *) TM_SHARED_READ_P(p->left); // note: l must have key infinity, and l->left must not.
    while (TM_SHARED_READ_P(l->left) != NULL) {
        gp = p;
        p = l;
        if (cmp(key, TM_SHARED_READ_L(p->key))) {
            l = (Node<K,V> *) TM_SHARED_READ_P(p->left);
        } else {
            l = (Node<K,V> *) TM_SHARED_READ_P(p->right);
        }
    }
    // if we fail to find the key in the tree
    if (key != TM_SHARED_READ_L(l->key)) {
        TM_END();
        shmem->enterQuiescentState(tid);
    } else {
        Node<K,V> *gpleft, *gpright;
        Node<K,V> *pleft, *pright;
        Node<K,V> *sleft, *sright;
        gpleft = (Node<K,V> *) TM_SHARED_READ_P(gp->left);
        gpright = (Node<K,V> *) TM_SHARED_READ_P(gp->right);
        pleft = (Node<K,V> *) TM_SHARED_READ_P(p->left);
        pright = (Node<K,V> *) TM_SHARED_READ_P(p->right);
        // assert p is a child of gp, l is a child of p
        Node<K,V> *s = (l == pleft ? pright : pleft);
        sleft = (Node<K,V> *) TM_SHARED_READ_P(s->left);
        sright = (Node<K,V> *) TM_SHARED_READ_P(s->right);
        if (p == gpleft) {
            TM_SHARED_WRITE_P(gp->left, s);
        } else {
            TM_SHARED_WRITE_P(gp->right, s);
        }
        result = TM_SHARED_READ_L(l->value);
        TM_END();
        shmem->enterQuiescentState(tid);

        // do memory reclamation and allocation
        shmem->retire(tid, p);
        shmem->retire(tid, l);
    }
    return pair<V,bool>(result, (result != NO_VALUE));
}
#endif

template<class K, class V, class Compare, class RecManager>
inline bool bst<K,V,Compare,RecManager>::updateInsert_txn_search_inplace(
            ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) {
    const K& key = *((const K*) input[0]);
    const V& val = *((const V*) input[1]);
    const bool onlyIfAbsent = *((const bool*) input[2]);
    V *result = (V*) output[0];

    TRACE COUTATOMICTID("updateInsert_txn_search_inplace(tid="<<tid<<", key="<<key<<")"<<endl);

    initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, /*1,*/ NULL, NULL);
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        if (info->path == PATH_FAST_HTM && !ALLOWABLE_PATH_CONCURRENCY[P1NUM][P3NUM] && numFallback.load(memory_order_relaxed) > 0) XABORT(ABORT_PROCESS_ON_FALLBACK);
        Node<K,V> *p = root, *l;
        l = root->left;
        if (l->left != NULL) { // the tree contains some node besides sentinels...
            p = l;
            l = l->left;    // note: l must have key infinity, and l->left must not.
            while (l->left != NULL) {
                p = l;
                if (cmp(key, p->key)) {
                    l = p->left;
                } else {
                    l = p->right;
                }
            }
        }
        // if we find the key in the tree already
        if (key == l->key) {
            if (onlyIfAbsent) {
                XEND();
                *result = val; // for insertIfAbsent, we don't care about the particular value, just whether we inserted or not. so, we use val to signify not having inserted (and NO_VALUE to signify having inserted).
                this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
                return true; // success
            } else {
                *result = l->value;
                l->value = val;
                XEND();
                this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
//                this->counters->updateChange[info->path]->inc(tid);
                return true;
            }
        } else {
            //int newWeight = (IS_SENTINEL(l, p) ? 1 : l->weight - 1); // Compute the weight for the new parent node. If l is a sentinel then we must set its weight to one.
            //l->weight = 1;
            //initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, /*1,*/ NULL, NULL);
            if (l->key == NO_KEY || cmp(key, l->key)) {
                initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), l->key, l->value, /*newWeight,*/ GET_ALLOCATED_NODE_PTR(tid, 0), l);
            } else {
                initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), key, val, /*newWeight,*/ l, GET_ALLOCATED_NODE_PTR(tid, 0));
            }
            *result = NO_VALUE;

            Node<K,V> *pleft = p->left;
//            Node<K,V> *pright = p->right;            
            if (l == pleft) {
                p->left = GET_ALLOCATED_NODE_PTR(tid, 1);
            } else {
//                assert(l == pright);
                p->right = GET_ALLOCATED_NODE_PTR(tid, 1);
            }
            XEND();
            this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
//            this->counters->updateChange[info->path]->inc(tid);
            
            // do memory reclamation and allocation
            REPLACE_ALLOCATED_NODE(tid, 0);
            REPLACE_ALLOCATED_NODE(tid, 1);
            
            return true;
        }
    } else {
aborthere:
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        if (status & _XABORT_CAPACITY) info->capacityAborted[info->path] = true;
        info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        return false;
    }
}

template<class K, class V, class Compare, class RecManager>
inline bool bst<K,V,Compare,RecManager>::updateInsert_txn_search_replace_markingw_infowr(
            ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) {
    const K& key = *((const K*) input[0]);
    const V& val = *((const V*) input[1]);
    const bool onlyIfAbsent = *((const bool*) input[2]);
    V *result = (V*) output[0];
    
    TRACE COUTATOMICTID("updateInsert_txn_search_replace_markingw_infowr(tid="<<tid<<", key="<<key<<")"<<endl);
    
    SCXRecord<K,V>* scx = (SCXRecord<K,V>*) NEXT_VERSION_NUMBER(tid);
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        if (info->path == PATH_FAST_HTM && !ALLOWABLE_PATH_CONCURRENCY[P1NUM][P3NUM] && numFallback.load(memory_order_relaxed) > 0) XABORT(ABORT_PROCESS_ON_FALLBACK);
        Node<K,V> *p = root, *l;
        l = root->left;
        if (l->left != NULL) { // the tree contains some node besides sentinels...
            p = l;
            l = l->left;    // note: l must have key infinity, and l->left must not.
            while (l->left != NULL) {
                p = l;
                if (cmp(key, p->key)) {
                    l = p->left;
                } else {
                    l = p->right;
                }
            }
        }
        // if we find the key in the tree already
        if (key == l->key) {
            if (onlyIfAbsent) {
                XEND();
                TRACE COUTATOMICTID("return true5\n");
                this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
                *result = val; // for insertIfAbsent, we don't care about the particular value, just whether we inserted or not. so, we use val to signify not having inserted (and NO_VALUE to signify having inserted).
                return true; // success
            }
            Node<K,V> *pleft, *pright;
            if ((info->llxResults[0] = llx_intxn_markingwr_infowr(tid, p, &pleft, &pright)) == NULL) {
                XABORT(ABORT_NODE_POINTER_CHANGED);
            }
            // assert l is a child of p
            *result = l->value;
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, /*l->weight,*/ NULL, NULL);
            p->scxRecord = scx;
            l->marked = true;
            (l == pleft ? p->left : p->right) = GET_ALLOCATED_NODE_PTR(tid, 0);
            XEND();
            this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
//            this->counters->updateChange[info->path]->inc(tid);
            
            // do memory reclamation and allocation
            shmem->retire(tid, l);
            tryRetireSCXRecord(tid, (SCXRecord<K,V> *) info->llxResults[0], p);
            REPLACE_ALLOCATED_NODE(tid, 0);
            
            return true;
        } else {
            Node<K,V> *pleft, *pright;
            if ((info->llxResults[0] = llx_intxn_markingwr_infowr(tid, p, &pleft, &pright)) == NULL) {
                XABORT(ABORT_NODE_POINTER_CHANGED);
            }
            // assert l is a child of p
            //int newWeight = (IS_SENTINEL(l, p) ? 1 : l->weight - 1); // Compute the weight for the new parent node. If l is a sentinel then we must set its weight to one.
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, /*1,*/ NULL, NULL);
//            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), l->key, l->value, /*1,*/ NULL, NULL);
            if (l->key == NO_KEY || cmp(key, l->key)) {
                initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), l->key, l->value, /*newWeight,*/ GET_ALLOCATED_NODE_PTR(tid, 0), l);
            } else {
                initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), key, val, /*newWeight,*/ l, GET_ALLOCATED_NODE_PTR(tid, 0));
            }
            *result = NO_VALUE; 
            p->scxRecord = scx;
//            l->marked = true;
            (l == pleft ? p->left : p->right) = GET_ALLOCATED_NODE_PTR(tid, 1);
            XEND();
            this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
//            this->counters->updateChange[info->path]->inc(tid);
            
            // do memory reclamation and allocation
//            shmem->retire(tid, l);
            tryRetireSCXRecord(tid, (SCXRecord<K,V> *) info->llxResults[0], p);
            REPLACE_ALLOCATED_NODE(tid, 0);
            REPLACE_ALLOCATED_NODE(tid, 1);
//            REPLACE_ALLOCATED_NODE(tid, 2);
            
            return true;
        }
    } else {
aborthere:
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        if (status & _XABORT_CAPACITY) info->capacityAborted[info->path] = true;
        info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        return false;
    }
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
    l = root->left;
    if (l->left != NULL) { // the tree contains some node besides sentinels...
        p = l;
        l = l->left;    // note: l must have key infinity, and l->left must not.
        while (l->left != NULL) {
            p = l;
            if (cmp(key, p->key)) {
                l = p->left;
            } else {
                l = p->right;
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
        info->numberOfNodesToReclaim = 1;
        info->numberOfNodesAllocated = 1;
        info->type = SCXRecord<K,V>::TYPE_REPLACE;
        info->nodes[0] = p;
        info->nodes[1] = l;
        bool retval = scx(tid, info, (l == pleft ? &p->left : &p->right), GET_ALLOCATED_NODE_PTR(tid, 0));
        if (retval) {
//            this->counters->updateChange[info->path]->inc(tid);
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
        // Compute the weight for the new parent node.
        // If l is a sentinel then we must set its weight to one.
        //int newWeight = (IS_SENTINEL(l, p) ? 1 : l->weight - 1);
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, /*1,*/ NULL, NULL);
//        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), l->key, l->value, /*1,*/ NULL, NULL);
        // TODO: change all equality comparisons with NO_KEY to use cmp()
        if (l->key == NO_KEY || cmp(key, l->key)) {
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), l->key, l->value, /*newWeight,*/ GET_ALLOCATED_NODE_PTR(tid, 0), l);
        } else {
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), key, val, /*newWeight,*/ l, GET_ALLOCATED_NODE_PTR(tid, 0));
        }
        *result = NO_VALUE;
        info->numberOfNodes = 2;
        info->numberOfNodesToReclaim = 0;
        info->numberOfNodesToFreeze = 1; // only freeze nodes[0]
        info->numberOfNodesAllocated = 2;//3;
        info->type = SCXRecord<K,V>::TYPE_INS;
        info->nodes[0] = p;
        info->nodes[1] = l; // note: used as OLD value for CAS that changes p's child pointer (but is not frozen or marked)
        bool retval = scx(tid, info, (l == pleft ? &p->left : &p->right), GET_ALLOCATED_NODE_PTR(tid, 1));
        if (retval) {
//            this->counters->updateChange[info->path]->inc(tid);
        }
        return retval;
    }
}

template<class K, class V, class Compare, class RecManager>
inline bool bst<K,V,Compare,RecManager>::updateInsert_lock_search_inplace(
            ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) {
    const K& key = *((const K*) input[0]);
    const V& val = *((const V*) input[1]);
    const bool onlyIfAbsent = *((const bool*) input[2]);
    V *result = (V*) output[0];

    TRACE COUTATOMICTID("updateInsert_lock_search_update(tid="<<tid<<", key="<<key<<")"<<endl);

    acquireLock(&this->lock);
    Node<K,V> *p = root, *l;
    l = root->left;
    if (l->left != NULL) { // the tree contains some node besides sentinels...
        p = l;
        l = l->left;    // note: l must have key infinity, and l->left must not.
        while (l->left != NULL) {
            p = l;
            if (cmp(key, p->key)) {
                l = p->left;
            } else {
                l = p->right;
            }
        }
    }
    // if we find the key in the tree already
    if (key == l->key) {
        if (onlyIfAbsent) {
            releaseLock(&this->lock);
            *result = val; // for insertIfAbsent, we don't care about the particular value, just whether we inserted or not. so, we use val to signify not having inserted (and NO_VALUE to signify having inserted).
            return true; // success
        } else {
            *result = l->value;
//            Node<K,V> *pleft = p->left;            
//            Node<K,V> *pright = p->right;            
//            if (l != pleft && l != pright) { // impossible with global lock
//                ds_releaseLock(&this->lock);
//                return false;
//            }
            l->value = val;
            releaseLock(&this->lock);
//            this->counters->updateChange[info->path]->inc(tid);
            return true;
        }
    } else {
        //int newWeight = (IS_SENTINEL(l, p) ? 1 : l->weight - 1); // Compute the weight for the new parent node. If l is a sentinel then we must set its weight to one.
        //l->weight = 1;
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), key, val, /*1,*/ NULL, NULL);
        if (l->key == NO_KEY || cmp(key, l->key)) {
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), l->key, l->value, /*newWeight,*/ GET_ALLOCATED_NODE_PTR(tid, 0), l);
        } else {
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 1), key, val, /*newWeight,*/ l, GET_ALLOCATED_NODE_PTR(tid, 0));
        }

        Node<K,V> *pleft = p->left;            
//        Node<K,V> *pright = p->right;            
        if (l == pleft) {
            p->left = GET_ALLOCATED_NODE_PTR(tid, 1);
        } else {
//            assert(l == pright);
            p->right = GET_ALLOCATED_NODE_PTR(tid, 1);
        }
        releaseLock(&this->lock);
//        this->counters->updateChange[info->path]->inc(tid);
        *result = NO_VALUE;
        
        // do memory reclamation and allocation
        shmem->retire(tid, l);
        REPLACE_ALLOCATED_NODE(tid, 0);
        REPLACE_ALLOCATED_NODE(tid, 1);

        return true;
    }
}

template<class K, class V, class Compare, class RecManager>
inline bool bst<K,V,Compare,RecManager>::updateErase_txn_search_inplace(
            ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) { // input consists of: const K& key
    const K& key = *((const K*) input[0]);
    V *result = (V*) output[0];
//    bool *shouldRebalance = (bool*) output[1];

    TRACE COUTATOMICTID("updateErase_txn_search_inplace(tid="<<tid<<", key="<<key<<")"<<endl);

TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        if (info->path == PATH_FAST_HTM && !ALLOWABLE_PATH_CONCURRENCY[P1NUM][P3NUM] && numFallback.load(memory_order_relaxed) > 0) XABORT(ABORT_PROCESS_ON_FALLBACK);
        Node<K,V> *gp, *p, *l;
        l = root->left;
        if (l->left == NULL) {
            XEND();
            this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
            *result = NO_VALUE;
            return true;
        } // only sentinels in tree...
        gp = root;
        p = l;
        l = p->left;    // note: l must have key infinity, and l->left must not.
        while (l->left != NULL) {
            gp = p;
            p = l;
            if (cmp(key, p->key)) {
                l = p->left;
            } else {
                l = p->right;
            }
        }
        // if we fail to find the key in the tree
        if (key != l->key) {
            XEND();
            this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
            *result = NO_VALUE;
            return true; // success
        } else {
            Node<K,V> *gpleft, *gpright;
            Node<K,V> *pleft, *pright;
            Node<K,V> *sleft, *sright;
            gpleft = gp->left;
            gpright = gp->right;
            pleft = p->left;
            pright = p->right;
            // assert p is a child of gp, l is a child of p
            Node<K,V> *s = (l == pleft ? pright : pleft);
            sleft = s->left;
            sright = s->right;
            //int newWeight = (IS_SENTINEL(p, gp) ? 1 : p->weight + s->weight); // Compute weight for the new node that replaces p (and l). If p is a sentinel then we must set the new node's weight to one.
            //s->weight = newWeight;
            if (p == gpleft) {
                gp->left = s;
            } else {
                gp->right = s;
            }
            *result = l->value;
            XEND();
            this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
//            this->counters->updateChange[info->path]->inc(tid);

            // do memory reclamation and allocation
            shmem->retire(tid, p);
            shmem->retire(tid, l);
            tryRetireSCXRecord(tid, p->scxRecord, p);

            return true;
        }
    } else { // transaction failed
aborthere:
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        if (status & _XABORT_CAPACITY) info->capacityAborted[info->path] = true;
        info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        return false;
    }
}

template<class K, class V, class Compare, class RecManager>
inline bool bst<K,V,Compare,RecManager>::updateErase_txn_search_replace_markingw_infowr(
            ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) { // input consists of: const K& key
    const K& key = *((const K*) input[0]);
    V *result = (V*) output[0];
//    bool *shouldRebalance = (bool*) output[1];

    TRACE COUTATOMICTID("updateErase_txn_search_replace_markingw_infowr(tid="<<tid<<", key="<<key<<")"<<endl);

    SCXRecord<K,V>* scx = (SCXRecord<K,V>*) NEXT_VERSION_NUMBER(tid);
    Node<K,V> *gp, *p, *l;
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        if (info->path == PATH_FAST_HTM && !ALLOWABLE_PATH_CONCURRENCY[P1NUM][P3NUM] && numFallback.load(memory_order_relaxed) > 0) XABORT(ABORT_PROCESS_ON_FALLBACK);
        l = root->left;
        if (l->left == NULL) {
            XEND();
            this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
            *result = NO_VALUE;
            return true;
        } // only sentinels in tree...
        gp = root;
        p = l;
        l = p->left;    // note: l must have key infinity, and l->left must not.
        while (l->left != NULL) {
            gp = p;
            p = l;
            if (cmp(key, p->key)) {
                l = p->left;
            } else {
                l = p->right;
            }
        }
        // if we fail to find the key in the tree
        if (key != l->key) {
            XEND();
            this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
            *result = NO_VALUE;
            return true; // success
        } else {
            Node<K,V> *gpleft, *gpright;
            Node<K,V> *pleft, *pright;
            Node<K,V> *sleft, *sright;
            if ((info->llxResults[0] = llx_intxn_markingwr_infowr(tid, gp, &gpleft, &gpright)) == NULL) XABORT(ABORT_LLX_FAILED); //return false;
            if ((info->llxResults[1] = llx_intxn_markingwr_infowr(tid, p, &pleft, &pright)) == NULL) XABORT(ABORT_LLX_FAILED); //return false;
            *result = l->value;
            // Read fields for the sibling s of l
            Node<K,V> *s = (l == pleft ? pright : pleft);
            if ((info->llxResults[2] = llx_intxn_markingwr_infowr(tid, s, &sleft, &sright)) == NULL) XABORT(ABORT_LLX_FAILED); //return false;
            // Now, if the op. succeeds, all structure is guaranteed to be just as we verified
            //int newWeight = (IS_SENTINEL(p, gp) ? 1 : p->weight + s->weight); // Compute weight for the new node that replaces p (and l). If p is a sentinel then we must set the new node's weight to one.
            initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), s->key, s->value, /*newWeight,*/ sleft, sright);
//            info->numberOfNodesAllocated = 1;
            //info->numberOfNodesToReclaim = 3;
//            info->type = SCXRecord<K,V>::TYPE_DEL;
//            info->nodes[0] = gp;
//            info->nodes[1] = p;
//            info->nodes[2] = s;
//            info->nodes[3] = l;
//            scx_intxn_markingwr_infowr(tid, info, (p == gpleft ? &gp->left : &gp->right), GET_ALLOCATED_NODE_PTR(tid, 0));
            // scx
            gp->scxRecord = scx;
            p->scxRecord = scx;
            p->marked = true;
            s->scxRecord = scx;
            s->marked = true;
            //l->marked = true; // l is known to be a leaf, so it doesn't need to be marked
            (p == gpleft ? gp->left : gp->right) = GET_ALLOCATED_NODE_PTR(tid, 0);
            XEND();
            this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
//            this->counters->updateChange[info->path]->inc(tid);
            
            // do memory reclamation and allocation
            shmem->retire(tid, p);
            shmem->retire(tid, s);
            shmem->retire(tid, l);
            tryRetireSCXRecord(tid, (SCXRecord<K,V> *) info->llxResults[0], gp);
            tryRetireSCXRecord(tid, (SCXRecord<K,V> *) info->llxResults[1], p);
            tryRetireSCXRecord(tid, (SCXRecord<K,V> *) info->llxResults[2], s);
            REPLACE_ALLOCATED_NODE(tid, 0);

            return true;
        }
    } else {
aborthere:
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        if (status & _XABORT_CAPACITY) info->capacityAborted[info->path] = true;
        info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        return false;
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
    l = root->left;
    if (l->left == NULL) {
        *result = NO_VALUE;
        return true;
    } // only sentinels in tree...
    gp = root;
    p = l;
    l = p->left;    // note: l must have key infinity, and l->left must not.
    while (l->left != NULL) {
        gp = p;
        p = l;
        if (cmp(key, p->key)) {
            l = p->left;
        } else {
            l = p->right;
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
        // Compute weight for the new node that replaces p (and l)
        // If p is a sentinel then we must set the new node's weight to one.
        //int newWeight = (IS_SENTINEL(p, gp) ? 1 : p->weight + s->weight);
        initializeNode(tid, GET_ALLOCATED_NODE_PTR(tid, 0), s->key, s->value, /*newWeight,*/ sleft, sright);
        info->numberOfNodes = 4;
        info->numberOfNodesToReclaim = 3;
        info->numberOfNodesToFreeze = 3;
        info->numberOfNodesAllocated = 1;
        info->type = SCXRecord<K,V>::TYPE_DEL;
        info->nodes[0] = gp;
        info->nodes[1] = p;
        info->nodes[2] = s;
        info->nodes[3] = l;
        bool retval = scx(tid, info, (p == gpleft ? &gp->left : &gp->right), GET_ALLOCATED_NODE_PTR(tid, 0));
        if (retval) {
//            this->counters->updateChange[info->path]->inc(tid);
        }
        return retval;
    }
}

template<class K, class V, class Compare, class RecManager>
inline bool bst<K,V,Compare,RecManager>::updateErase_lock_search_inplace(
            ReclamationInfo<K,V> * const info, const int tid, void **input, void **output) { // input consists of: const K& key
    const K& key = *((const K*) input[0]);
    V *result = (V*) output[0];
//    bool *shouldRebalance = (bool*) output[1];

    TRACE COUTATOMICTID("updateErase_lock_search_inplace(tid="<<tid<<", key="<<key<<")"<<endl);

    acquireLock(&this->lock);
    Node<K,V> *gp, *p, *l;
    l = root->left;
    if (l->left == NULL) {
        ds_releaseLock(&this->lock);
        *result = NO_VALUE;
        return true;
    } // only sentinels in tree...
    gp = root;
    p = l;
    l = p->left;    // note: l must have key infinity, and l->left must not.
    while (l->left != NULL) {
        gp = p;
        p = l;
        if (cmp(key, p->key)) {
            l = p->left;
        } else {
            l = p->right;
        }
    }
    // if we fail to find the key in the tree
    if (key != l->key) {
        releaseLock(&this->lock);
        *result = NO_VALUE;
        return true; // success
    } else {
        Node<K,V> *gpleft, *gpright;
        Node<K,V> *pleft, *pright;
        gpleft = gp->left;
        gpright = gp->right;
        pleft = p->left;
        pright = p->right;
        // assert p is a child of gp, l is a child of p
        Node<K,V> *s = (l == pleft ? pright : pleft);
        if (p == gpleft) {
            gp->left = s;
        } else {
            gp->right = s;
        }
        *result = l->value;
        releaseLock(&this->lock);
//        this->counters->updateChange[info->path]->inc(tid);
        
        // do memory reclamation and allocation
        shmem->retire(tid, p);
        shmem->retire(tid, l);
        tryRetireSCXRecord(tid, p->scxRecord, p);

        return true;
    }
}

// this internal function is called only by scx(), and only when otherSCX is protected by a call to shmem->protect
template<class K, class V, class Compare, class RecManager>
inline bool bst<K,V,Compare,RecManager>::tryRetireSCXRecord(const int tid, SCXRecord<K,V> * const otherSCX, Node<K,V> * const node) {
    if (otherSCX == dummy) return false; // never retire the dummy scx record!
    if (IS_VERSION_NUMBER(otherSCX)) return false; // can't retire version numbers!
    if (otherSCX->state == SCXRecord<K,V>::STATE_COMMITTED) {
        // in this tree, committed scx records are only pointed to by one node.
        // so, when this function is called, the scx record is already retired.
        shmem->retire(tid, otherSCX);
        return true;
    } else { // assert: scx->state >= STATE_ABORTED
#ifdef OLD_SCXRECORD_RECLAMATION
        const int state_aborted = SCXRecord<K,V>::STATE_ABORTED;
        assert(otherSCX->state >= state_aborted); /* state is aborted */
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
                    int stateOld = otherSCX->state;
                    stateNew = STATE_GET_WITH_FLAG_OFF(stateOld, i);
                    DEBUG assert(stateOld >= state_aborted);
                    DEBUG assert(stateNew >= state_aborted);
                    assert(stateNew < stateOld);
                    casSucceeded = __sync_bool_compare_and_swap(&otherSCX->state, stateOld, stateNew);//otherSCX->state.compare_exchange_weak(stateOld, stateNew);       // MEMBAR ON X86/64
                }
                break;
            }
        }
        // many scxs can all be CASing state and trying to retire this node.
        // the one who gets to invoke retire() is the one whose CAS sets
        // the flag subfield of scx->state to 0.
        if (casSucceeded && STATE_GET_FLAGS(stateNew) == 0) {
            shmem->retire(tid, otherSCX);
            return true;
        }
#else
        int stateOld = otherSCX->state;
        const int state_aborted = SCXRecord<K,V>::STATE_ABORTED;
        assert(stateOld >= state_aborted);
        int result = __sync_add_and_fetch(&otherSCX->state, stateOld, -STATE_REFCOUNT_UNIT);
        if (STATE_GET_REFCOUNT(result) == 0) {
            shmem->retire(tid, otherSCX);
            return true;
        }
#endif
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
    
//    /** begin debug **/
//    REPLACE_ALLOCATED_SCXRECORD(tid);
//    for (int i=0;i<info->numberOfNodesAllocated;++i) {
//        REPLACE_ALLOCATED_NODE(tid, i);
//    }
//    return;
//    /** end debug **/
    
    Node<K,V> ** const nodes = info->nodes;
    SCXRecord<K,V> * const * const scxRecordsSeen = (SCXRecord<K,V> * const * const) info->llxResults;
    const int state = info->state;
    const int operationType = info->type;
    
    // NOW, WE ATTEMPT TO RECLAIM ANY RETIRED NODES AND SCX RECORDS
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
        assert(!shmem->supportsCrashRecovery() || shmem->isQuiescent(tid));
        REPLACE_ALLOCATED_SCXRECORD(tid);

        // if the state was COMMITTED, then we cannot reuse the nodes the we
        // took from allocatedNodes[], either, so we must replace these nodes.
        // for the chromatic tree, the number of nodes can be found in
        // NUM_INSERTS[operationType].
        // in general, we have to add a parameter, specified when you call SCX,
        // that says how large the replacement subtree of new nodes is.
        // alternatively, we could just move this out into the data structure code,
        // to be performed AFTER an scx completes.
        if (state == SCXRecord<K,V>::STATE_COMMITTED) {
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
            DEBUG { // some debug invariant checking
                if (j>0) { // nodes[0] could be reclaimed already, so nodes[0]->left is not safe
                    if (state == SCXRecord<K,V>::STATE_COMMITTED) {
                        // NOTE: THE FOLLOWING ACCESSES TO NODES[J]'S FIELDS ARE
                        //       ONLY SAFE BECAUSE STATE IS NOT ABORTED!
                        //       (also, we are the only one who can invoke retire[j],
                        //        and we have not done so yet.)
                        //       IF STATE == ABORTED, THE NODE MAY BE RECLAIMED ALREADY!
                        if (nodes[j]->left == NULL) {
                            if (!(nodes[j]->scxRecord == dummy)) {
//                                COUTATOMICTID("(SCXRecord<K,V> *) nodes["<<j<<"]->scxRecord="<<(nodes[j]->scxRecord)<<endl);
//                                COUTATOMICTID("dummy="<<dummy<<endl);
                                assert(false);
                            }
                            if (scxRecordsSeen[j] != LLX_RETURN_IS_LEAF) {
//                                if (nodes[j]) { COUTATOMICTID("nodes["<<j<<"]="<<*nodes[j]<<endl); }
//                                else { COUTATOMICTID("nodes["<<j<<"]=NULL"<<endl); }
//                                //if (newNode) { COUTATOMICTID("newNode="<<*newNode<<endl); }
//                                //else { COUTATOMICTID("newNode=NULL"<<endl); }
//                                COUTATOMICTID("scxRecordsSeen["<<j<<"]="<<scxRecordsSeen[j]<<endl);
//                                COUTATOMICTID("dummy="<<dummy<<endl);
                                assert(false);
                            }
                        }
                    }
                } else {
                    assert(scxRecordsSeen[j] != LLX_RETURN_IS_LEAF);
                }
            }
            // if nodes[j] is not a leaf, then we froze it, changing the scx record
            // that nodes[j] points to. so, we try to retire the scx record is
            // no longer pointed to by nodes[j].
            // note: we know scxRecordsSeen[j] is not retired, since we have not
            //       zeroed out its flag representing an incoming pointer
            //       from nodes[j] until we execute tryRetireSCXRecord() below.
            //       (it follows that we don't need to invoke protect().)
            if (scxRecordsSeen[j] != LLX_RETURN_IS_LEAF && !IS_VERSION_NUMBER(scxRecordsSeen[j])) {
                bool success = tryRetireSCXRecord(tid, scxRecordsSeen[j], nodes[j]);
                DEBUG2 {
                    // note: tryRetireSCXRecord returns whether it retired an scx record.
                    //       this code checks that we don't retire the same scx record twice!
                    if (success && scxRecordsSeen[j] != dummy) {
                        for (int k=j+1;k<highestIndexReached;++k) {
                            assert(scxRecordsSeen[j] != scxRecordsSeen[k]);
                        }
                    }
                }
            }
        }
        SOFTWARE_BARRIER; // prevent compiler from moving retire() calls before tryRetireSCXRecord() calls above
        if (state == SCXRecord<K,V>::STATE_COMMITTED) {
            //const int nNodes = info->numberOfNodes;
            const int nNodes = info->numberOfNodesToReclaim;
            // nodes[1], nodes[2], ..., nodes[nNodes-1] are now retired
            for (int j=1;j<1+nNodes;++j) {
                DEBUG if (j < highestIndexReached) {
                    if ((void*) scxRecordsSeen[j] != LLX_RETURN_IS_LEAF) {
                        assert(nodes[j]->scxRecord == debugSCXRecord);
                        assert(nodes[j]->marked);
                    }
                }
                shmem->retire(tid, nodes[j]);
            }
        } else {
            assert(state >= state_aborted); /* is ABORTED */
        }
    }
}

template<class K, class V, class Compare, class RecManager>
void bst<K,V,Compare,RecManager>::htmWrapper(
            UPDATE_FUNCTION(update_for_fastHTM),
            UPDATE_FUNCTION(update_for_slowHTM),
            UPDATE_FUNCTION(update_for_fallback),
            const int tid,
            void **input,
            void **output) {
    ReclamationInfo<K,V> info;
//#ifdef WAIT_FOR_FALLBACK
//    info.path = (MAX_FAST_HTM_RETRIES >= 0
//        ? (numFallback > 0
//                ? (MAX_SLOW_HTM_RETRIES >= 0 ? PATH_SLOW_HTM : PATH_FALLBACK)
//                : PATH_FAST_HTM)
//        : MAX_SLOW_HTM_RETRIES >= 0 ? PATH_SLOW_HTM : PATH_FALLBACK);
    info.path = (MAX_FAST_HTM_RETRIES >= 0 ? PATH_FAST_HTM : MAX_SLOW_HTM_RETRIES >= 0 ? PATH_SLOW_HTM : PATH_FALLBACK);
//#else
//    if (MAX_FAST_HTM_RETRIES >= 0 && MAX_SLOW_HTM_RETRIES >= 0) { // THREE PATH SCHEME
//        if (ALLOWABLE_PATH_CONCURRENCY[P1NUM][P3NUM] || numFallback.load(memory_order_relaxed) == 0) {
//            info.path = PATH_FAST_HTM;
//        } else {
//            info.path = PATH_SLOW_HTM;
//        }
//    } else { // TWO OR ONE PATH SCHEME
//        if (MAX_FAST_HTM_RETRIES >= 0) { // TWO PATH SCHEME: FAST, FALLBACK
//            info.path = PATH_FAST_HTM;
//        } else if (MAX_SLOW_HTM_RETRIES >= 0) { // TWO PATH SCHEME: SLOW, FALLBACK
//            info.path = PATH_SLOW_HTM;
//        } else { // ONE PATH SCHEME: FALLBACK
//            info.path = PATH_FALLBACK;
//        }
//    }
//#endif
    int attempts = 0;
    bool finished = 0;
    info.capacityAborted[PATH_FAST_HTM] = 0;
    info.capacityAborted[PATH_SLOW_HTM] = 0;
    info.lastAbort = 0;
    for (;;) {
        switch (info.path) {
            case PATH_FAST_HTM:
                shmem->leaveQuiescentState(tid);
                finished = (this->*update_for_fastHTM)(&info, tid, input, output);
                shmem->enterQuiescentState(tid);
                if (finished) {
                    this->counters->pathSuccess[info.path]->inc(tid);
                    this->counters->pathFail[info.path]->add(tid, attempts);
                    return;
                } else {
//                    if (info.lastAbort == 0) {
//                        numFallback.fetch_add(1);
//                        info.path = PATH_FALLBACK;
//                        continue;
//                    } /* DEBUG */
                    // check if we should change paths
                    ++attempts;
//#ifdef WAIT_FOR_FALLBACK
                    // TODO: move to middle immediately if a process is on the fallback path
                    if (attempts > MAX_FAST_HTM_RETRIES) {
                        this->counters->pathFail[info.path]->add(tid, attempts);
                        attempts = 0;
                        // check if we aren't allowing slow htm path
                        if (MAX_SLOW_HTM_RETRIES < 0) {
                            info.path = PATH_FALLBACK;
                            numFallback.fetch_add(1);
                        } else {
                            info.path = PATH_SLOW_HTM;
                        }
                    /* MOVE TO THE MIDDLE PATH IMMEDIATELY IF SOMEONE IS ON THE FALLBACK PATH */ \
                    } else if ((info.lastAbort >> 24) == ABORT_PROCESS_ON_FALLBACK && MAX_SLOW_HTM_RETRIES >= 0) { /* DEBUG */
                        attempts = 0;
                        info.path = PATH_SLOW_HTM;
                        //continue;
                    /* if there is no middle path, wait for the fallback path to be empty */ \
                    } else if (MAX_SLOW_HTM_RETRIES < 0) {
                        while (numFallback.load(memory_order_relaxed) > 0) { __asm__ __volatile__("pause;"); }
                    }
//#else
//                    if ((MAX_SLOW_HTM_RETRIES >= 0 && getStatusExplicitAbortCode(info.lastAbort) == ABORT_PROCESS_ON_FALLBACK) // move to middle path immediately if we aborted because of a process on the fallback path (and we are executing a 3-path alg)
//                                || attempts > MAX_FAST_HTM_RETRIES) {
//                        this->counters->pathFail[info.path]->add(tid, attempts);
//                        attempts = 0;
//                        // check if we aren't allowing slow htm path
//                        if (MAX_SLOW_HTM_RETRIES < 0) {
//                            info.path = PATH_FALLBACK;
//                            if (MAX_FAST_HTM_RETRIES >= 0) numFallback.fetch_add(1);
//                        } else {
//                            info.path = PATH_SLOW_HTM;
//                        }
//                    }
//#endif
                }
                break;
            case PATH_SLOW_HTM:
                shmem->leaveQuiescentState(tid);
                finished = (this->*update_for_slowHTM)(&info, tid, input, output);
                shmem->enterQuiescentState(tid);
                if (finished) {
                    this->counters->pathSuccess[info.path]->inc(tid);
                    this->counters->pathFail[info.path]->add(tid, attempts);
                    return;
                } else {
//                    if (info.lastAbort == 0) {
//                        numFallback.fetch_add(1);
//                        info.path = PATH_FALLBACK;
//                        continue;
//                    } /* DEBUG */
                    // check if we should change paths
                    ++attempts;
                    if (attempts > MAX_SLOW_HTM_RETRIES) {
                        this->counters->pathFail[info.path]->add(tid, attempts);
                        attempts = 0;
                        info.path = PATH_FALLBACK;
                        if (MAX_FAST_HTM_RETRIES >= 0 || MAX_SLOW_HTM_RETRIES >= 0) numFallback.fetch_add(1);
                    }
                }
                break;
            case PATH_FALLBACK:
                shmem->leaveQuiescentState(tid);
                finished = (this->*update_for_fallback)(&info, tid, input, output);
                shmem->enterQuiescentState(tid);
                if (finished) {
                    this->counters->pathSuccess[info.path]->inc(tid);
                    if (MAX_FAST_HTM_RETRIES >= 0 || MAX_SLOW_HTM_RETRIES >= 0) numFallback.fetch_add(-1);
                    return;
                } else {
                    this->counters->pathFail[info.path]->inc(tid);
                }
                break;
            default:
                cout<<"reached impossible switch case"<<endl;
                exit(-1);
                break;
        }
    }
}

/**
 * CAN BE INVOKED ONLY IF EVERYTHING FROM THE FIRST LINKED LLX
 * TO THE END OF THIS CALL WILL BE EXECUTED ATOMICALLY
 * (E.G., IN A TXN OR CRITICAL SECTION)
 */
template<class K, class V, class Compare, class RecManager>
__rtm_force_inline bool bst<K,V,Compare,RecManager>::scx_intxn_markingwr_infowr(
            const int tid,
            ReclamationInfo<K,V> * const info,
            Node<K,V> * volatile * field,        // pointer to a "field pointer" that will be changed
            Node<K,V> *newNode) {
    // warning: needs to go up to numberOfNodes for marking...
    SCXRecord<K,V>* scx = (SCXRecord<K,V>*) NEXT_VERSION_NUMBER(tid);
    switch(info->numberOfNodesToFreeze) {
        case 7: info->nodes[6]->marked = true; info->nodes[6]->scxRecord = scx;
        case 6: info->nodes[5]->marked = true; info->nodes[5]->scxRecord = scx;
        case 5: info->nodes[4]->marked = true; info->nodes[4]->scxRecord = scx;
        case 4: info->nodes[3]->marked = true; info->nodes[3]->scxRecord = scx;
        case 3: info->nodes[2]->marked = true; info->nodes[2]->scxRecord = scx;
        case 2: info->nodes[1]->marked = true; info->nodes[1]->scxRecord = scx;
        case 1: info->nodes[0]->scxRecord = scx;
    }
    *field = newNode;
    info->state = SCXRecord<K,V>::STATE_COMMITTED;
    return true;
}

// note: this needs to go up to NUM_OF_NODES for marking
template<class K, class V, class Compare, class RecManager>
__rtm_force_inline bool bst<K,V,Compare,RecManager>::scx_htm(
            const int tid,
            ReclamationInfo<K,V> * const info,
            Node<K,V> * volatile * field,        // pointer to a "field pointer" that will be changed
            Node<K,V> * newNode) {
    SCXRecord<K,V>* scx = (SCXRecord<K,V>*) NEXT_VERSION_NUMBER(tid);
    const int n = info->numberOfNodesToFreeze;
TXN1: int attempts = MAX_FAST_HTM_RETRIES;
    int status = XBEGIN();
    if (status == _XBEGIN_STARTED) {
        // abort if someone on the fastHTM or fallback path
        // changed nodes[i]->scxRecord since we last performed LLX on nodes[i].
        // note: the following switch block is just a manual unrolling of the following loop
        // (which might not be unrolled by the compiler because of the XABORT() volatile asm call)
        //        for (int i=0;i<n;++i) {
        //            if (nodes[i]->scxRecord != llxResults[i]) {
        //                XABORT(ABORT_SCXRECORD_POINTER_CHANGED);
        //            }
        //        }
        switch (n) {
            case 7: if (info->llxResults[6] != LLX_RETURN_IS_LEAF && info->nodes[6]->scxRecord != info->llxResults[6]) XABORT(ABORT_SCXRECORD_POINTER_CHANGED6);
            case 6: if (info->llxResults[5] != LLX_RETURN_IS_LEAF && info->nodes[5]->scxRecord != info->llxResults[5]) XABORT(ABORT_SCXRECORD_POINTER_CHANGED5);
            case 5: if (info->llxResults[4] != LLX_RETURN_IS_LEAF && info->nodes[4]->scxRecord != info->llxResults[4]) XABORT(ABORT_SCXRECORD_POINTER_CHANGED4);
            case 4: if (info->llxResults[3] != LLX_RETURN_IS_LEAF && info->nodes[3]->scxRecord != info->llxResults[3]) XABORT(ABORT_SCXRECORD_POINTER_CHANGED3);
            case 3: if (info->llxResults[2] != LLX_RETURN_IS_LEAF && info->nodes[2]->scxRecord != info->llxResults[2]) XABORT(ABORT_SCXRECORD_POINTER_CHANGED2);
            case 2: if (info->llxResults[1] != LLX_RETURN_IS_LEAF && info->nodes[1]->scxRecord != info->llxResults[1]) XABORT(ABORT_SCXRECORD_POINTER_CHANGED1);
            case 1: if (info->nodes[0]->scxRecord != info->llxResults[0]) XABORT(ABORT_SCXRECORD_POINTER_CHANGED0);
        }
        // note: the following switch block is just a manual unrolling of two loops, namely:
        //        for (int i=0;i<n;++i) {
        //            nodes[i]->scxRecord = info;
        //        }
        //        for (int i=1;i<n;++i) {
        //            nodes[i]->marked = true;
        //        }
        switch (n) {
            case 7: info->nodes[6]->scxRecord = scx; info->nodes[6]->marked = true;
            case 6: info->nodes[5]->scxRecord = scx; info->nodes[5]->marked = true;
            case 5: info->nodes[4]->scxRecord = scx; info->nodes[4]->marked = true;
            case 4: info->nodes[3]->scxRecord = scx; info->nodes[3]->marked = true;
            case 3: info->nodes[2]->scxRecord = scx; info->nodes[2]->marked = true;
            case 2: info->nodes[1]->scxRecord = scx; info->nodes[1]->marked = true;
            case 1: info->nodes[0]->scxRecord = scx;
        }
        *field = newNode;
        XEND();
        info->state = SCXRecord<K,V>::STATE_COMMITTED;
            this->counters->htmCommit[info->path]->inc(tid); //if (info->capacityAborted[info->path]) { this->counters->htmCapacityAbortThenCommit[info->path]->inc(tid); }
//        this->counters->updateChange[info->path]->inc(tid);
        return true;
    } else {
#ifdef RECORD_ABORTS
        this->counters->registerHTMAbort(tid, status, info->path);
#endif
//        if (status & _XABORT_CAPACITY) info->capacityAborted[info->path] = true;
        info->lastAbort = status;
        IF_ALWAYS_RETRY_WHEN_BIT_SET if (status & _XABORT_RETRY) { this->counters->pathFail[info->path]->inc(tid); this->counters->htmRetryAbortRetried[info->path]->inc(tid); goto TXN1; }
        return false;
    }
}

// you may call this only if each node in nodes is protected by a call to shmem->protect
template<class K, class V, class Compare, class RecManager>
__rtm_force_inline bool bst<K,V,Compare,RecManager>::scx(
            const int tid,
            ReclamationInfo<K,V> * const info,
            Node<K,V> * volatile * field,        // pointer to a "field pointer" that will be changed
            Node<K,V> * newNode) {
    TRACE COUTATOMICTID("scx(tid="<<tid<<" type="<<info->type<<")"<<endl);

    SCXRecord<K,V> *newscxrecord = GET_ALLOCATED_SCXRECORD_PTR(tid);
    initializeSCXRecord(tid, newscxrecord, info, field, newNode);
    
    // if this memory reclamation scheme supports crash recovery, it's important
    // that we protect the scx record and its nodes so we can help the scx complete
    // once we've recovered from the crash.
    if (shmem->supportsCrashRecovery()) {
        // it is important that initializeSCXRecord is performed before qProtect
        // because if we are suspected of crashing, we use the fact that isQProtected = true
        // to decide that we should finish our scx, and the results will be bogus
        // if our scx record is not initialized properly.
        SOFTWARE_BARRIER;
        for (int i=0;i<info->numberOfNodes;++i) {
            if (!shmem->qProtect(tid, info->nodes[i], callbackReturnTrue, NULL, false)) {
                assert(false);
                COUTATOMICTID("ERROR: failed to qProtect node"<<endl);
                exit(-1);
            }
        }
        for (int i=0;i<info->numberOfNodesToFreeze;++i) {
            if (!shmem->qProtect(tid, (SCXRecord<K,V>*) info->llxResults[i], callbackReturnTrue, NULL, false)) {
                assert(false);
                COUTATOMICTID("ERROR: failed to qProtect scx record in scxRecordsSeen / llxResults"<<endl);
                exit(-1);
            }
        }

        // it is important that we qprotect everything else before qprotecting our new
        // scx record, because the scx record is used to determine whether we should
        // help this scx once we've been suspected of crashing and have restarted,
        // and helping requires the nodes to be protected.
        // (we know the scx record is qprotected before the first freezing cas,
        //  so we know that no pointer to the scx record has been written to the 
        //  data structure if it is not qprotected when we execute the crash handler.)
        SOFTWARE_BARRIER;
        if (!shmem->qProtect(tid, newscxrecord, callbackReturnTrue, NULL, false)) {
            COUTATOMICTID("ERROR: failed to qProtect scx record"<<endl);
            assert(false); exit(-1);
        }
        // memory barriers are not needed for these qProtect() calls on x86/64
        // because there's no write-write reordering, and nothing can be
        // reordered over the first freezing CAS in help().
    }
    SOFTWARE_BARRIER;
    int state = help(tid, newscxrecord, false);
//    shmem->enterQuiescentState(tid);
    info->state = newscxrecord->state;
    reclaimMemoryAfterSCX(tid, info);
//    shmem->qUnprotectAll(tid);
    return state & SCXRecord<K,V>::STATE_COMMITTED;
}

// you may call this only if scx is protected by a call to shmem->protect.
// each node in scx->nodes must be protected by a call to shmem->protect.
// returns the state field of the scx record "scx."
template<class K, class V, class Compare, class RecManager>
__rtm_force_inline int bst<K,V,Compare,RecManager>::help(const int tid, SCXRecord<K,V> *scx, bool helpingOther) {
    assert(shmem->isProtected(tid, scx));
    assert(scx != dummy);
//    bool updateCAS = false;
    const int nNodes                        = scx->numberOfNodes;
    const int nFreeze                       = scx->numberOfNodesToFreeze;
    Node<K,V> ** const nodes                = scx->nodes;
    SCXRecord<K,V> ** const scxRecordsSeen  = scx->scxRecordsSeen;
    Node<K,V> * const newNode               = scx->newNode;
    TRACE COUTATOMICTID("help(tid="<<tid<<" scx="<<*scx<<" helpingOther="<<helpingOther<<"), nFreeze="<<nFreeze<<endl);
    //SOFTWARE_BARRIER; // prevent compiler from reordering read(state) before read(nodes), read(scxRecordsSeen), read(newNode). an x86/64 cpu will not reorder these reads.
    int __state = scx->state;
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
    
    DEBUG {
        for (int i=0;i<nNodes;++i) {
            assert(nodes[i] == root || shmem->isProtected(tid, nodes[i]));
        }
    }
    
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
#ifdef OLD_SCXRECORD_RECLAMATION
    int flags = 1; // bit i is 1 if nodes[i] is frozen and not a leaf, and 0 otherwise.
#else
    int freezeCount = 0;
#endif
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
        
        bool successfulCAS = __sync_bool_compare_and_swap(&nodes[i]->scxRecord, scxRecordsSeen[i], scx); // MEMBAR ON X86/64
        SCXRecord<K,V> * exp = nodes[i]->scxRecord;
        if (!successfulCAS && exp != scx) { // if work was not done
            if (scx->allFrozen) {
                assert(scx->state == 1); /*STATE_COMMITTED*/
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
                    scx->state = ABORT_STATE_INIT(0, 0);
                    return ABORT_STATE_INIT(0, 0); // scx is aborted (but no one else will ever know)
                } else {
                    // if this is the first failed freezing CAS to occur for this SCX,
                    // then flags encodes the pointers to this scx record from nodes IN the tree.
                    // (the following CAS will succeed only the first time it is performed
                    //  by any thread running help() for this scx.)
                    int expectedState = SCXRecord<K,V>::STATE_INPROGRESS;
#ifdef OLD_SCXRECORD_RECLAMATION
                    int newState = ABORT_STATE_INIT(i, flags);
#else
                    int newState = ABORT_STATE_INIT(i, freezeCount);
#endif
                    bool success = __sync_bool_compare_and_swap(&scx->state, expectedState, newState); // MEMBAR ON X86/64
                    assert(expectedState != 1); /* not committed */
                    // note2: a regular write will not do, here, since two people can start helping, one can abort at i>0, then after a long time, the other can fail to CAS i=0, so they can get different i values.
                    assert(scx->state & 2); /* SCXRecord<K,V>::STATE_ABORTED */
                    // ABORTED THE SCX AFTER PERFORMING ONE OR MORE SUCCESSFUL FREEZING CASs
                    if (success) {
                        TRACE COUTATOMICTID("help return ABORTED(changed to "<<newState<<") after failed freezing cas on nodes["<<i<<"]"<<endl);
                        return newState;
                    } else {
                        TRACE COUTATOMICTID("help return ABORTED(failed to change to "<<newState<<" because encountered "<<expectedState<<" instead of in progress) after failed freezing cas on nodes["<<i<<"]"<<endl);
                        return scx->state; // expectedState; // this has been overwritten by compare_exchange_strong with the value that caused the CAS to fail.
                    }
                }
            }
        } else {
#ifdef OLD_SCXRECORD_RECLAMATION
            flags |= (1<<i); // nodes[i] was frozen for scx
#else
            ++freezeCount;
#endif
            const int state_inprogress = SCXRecord<K,V>::STATE_INPROGRESS;
            assert(exp == scx || IS_VERSION_NUMBER((uintptr_t) exp) || (exp->state != state_inprogress));
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
    __sync_bool_compare_and_swap(scx->field, nodes[1], newNode);
    assert(scx->state < 2); // not aborted
    scx->state = SCXRecord<K,V>::STATE_COMMITTED;
    
    TRACE COUTATOMICTID("help return COMMITTED after performing update cas"<<endl);
    return SCXRecord<K,V>::STATE_COMMITTED; // success
}

template<class K, class V, class Compare, class RecManager>
__rtm_force_inline void *bst<K,V,Compare,RecManager>::llx_intxn_markingwr_infowr(
            const int tid,
            Node<K,V> *node,
            Node<K,V> **retLeft,
            Node<K,V> **retRight) {
    SCXRecord<K,V> *scx1 = node->scxRecord;
    int state = (IS_VERSION_NUMBER(scx1) ? SCXRecord<K,V>::STATE_COMMITTED : scx1->state);
    bool marked = node->marked;
    SOFTWARE_BARRIER;
    if (marked) {
        return NULL;
    } else {
        if ((state & SCXRecord<K,V>::STATE_COMMITTED /*&& !marked*/) || state & SCXRecord<K,V>::STATE_ABORTED) {
            *retLeft = node->left;
            *retRight = node->right;
            if (*retLeft == NULL) {
                return (void*) LLX_RETURN_IS_LEAF;
            }
            return scx1;
        }
    }
    return NULL; // fail
}

template<class K, class V, class Compare, class RecManager>
__rtm_force_inline void *bst<K,V,Compare,RecManager>::llx_htm(
            const int tid,
            Node<K,V> *node,
            Node<K,V> **retLeft,
            Node<K,V> **retRight) {
    SCXRecord<K,V> *scx1 = node->scxRecord;
    bool marked = node->marked;
    int state = (IS_VERSION_NUMBER(scx1) ? SCXRecord<K,V>::STATE_COMMITTED : scx1->state);
    SOFTWARE_BARRIER;       // prevent compiler from moving the read of marked before the read of state (no hw barrier needed on x86/64, since there is no read-read reordering)
    if ((state & SCXRecord<K,V>::STATE_COMMITTED && !marked) || state & SCXRecord<K,V>::STATE_ABORTED) {
        SOFTWARE_BARRIER;       // prevent compiler from moving the reads scx2=node->scxRecord or scx3=node->scxRecord before the read of marked. (no h/w barrier needed on x86/64 since there is no read-read reordering)
        *retLeft = node->left;
        *retRight = node->right;
        if (*retLeft == NULL) {
            TRACE COUTATOMICTID("llx return2.a (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<")\n"); 
            return (void*) LLX_RETURN_IS_LEAF;
        }
        SOFTWARE_BARRIER; // prevent compiler from moving the read of node->scxRecord before the read of left or right
        SCXRecord<K,V> *scx2 = node->scxRecord;
        if (scx1 == scx2) {
            return scx1;
        }
    }
    return NULL;           // fail
}

// you may call this only if node is protected by a call to shmem->protect
template<class K, class V, class Compare, class RecManager>
__rtm_force_inline void *bst<K,V,Compare,RecManager>::llx(
            const int tid,
            Node<K,V> *node,
            Node<K,V> **retLeft,
            Node<K,V> **retRight) {
    TRACE COUTATOMICTID("llx(tid="<<tid<<", node="<<*node<<")"<<endl);
    assert(node == root || shmem->isProtected(tid, node));
    bst_retired_info info;
    SCXRecord<K,V> *scx1 = node->scxRecord;
    IF_FAIL_TO_PROTECT_SCX(info, tid, scx1, &node->scxRecord, &node->marked) {
        TRACE COUTATOMICTID("llx return1 (tid="<<tid<<" key="<<node->key<<")\n");
        DEBUG counters->llxFail->inc(tid);
        return NULL;
    } // return and retry
    assert(scx1 == dummy || shmem->isProtected(tid, scx1));
    int state = (IS_VERSION_NUMBER(scx1) ? SCXRecord<K,V>::STATE_COMMITTED : scx1->state);
    SOFTWARE_BARRIER;       // prevent compiler from moving the read of marked before the read of state (no hw barrier needed on x86/64, since there is no read-read reordering)
    bool marked = node->marked;
    SOFTWARE_BARRIER;       // prevent compiler from moving the reads scx2=node->scxRecord or scx3=node->scxRecord before the read of marked. (no h/w barrier needed on x86/64 since there is no read-read reordering)
    if ((state & SCXRecord<K,V>::STATE_COMMITTED && !marked) || state & SCXRecord<K,V>::STATE_ABORTED) {
        SOFTWARE_BARRIER;       // prevent compiler from moving the reads scx2=node->scxRecord or scx3=node->scxRecord before the read of marked. (no h/w barrier needed on x86/64 since there is no read-read reordering)
        *retLeft = node->left;
        *retRight = node->right;
        if (*retLeft == NULL) {
            TRACE COUTATOMICTID("llx return2.a (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<")\n"); 
            return (void *) LLX_RETURN_IS_LEAF;
        }
        SOFTWARE_BARRIER; // prevent compiler from moving the read of node->scxRecord before the read of left or right
        SCXRecord<K,V> *scx2 = node->scxRecord;
        if (scx1 == scx2) {
            DEBUG {
                if (!IS_VERSION_NUMBER(scx1) && marked && state & SCXRecord<K,V>::STATE_ABORTED) {
                    // since scx1 == scx2, the two claims in the antecedent hold simultaneously.
                    assert(scx1 == dummy || shmem->isProtected(tid, scx1));
                    assert(node == root || shmem->isProtected(tid, node));
                    assert(node->marked);
                    assert(scx1->state & 2 /* aborted */);
                    assert(false);
                }
            }
            TRACE COUTATOMICTID("llx return2 (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<" scx1="<<scx1<<")\n"); 
            DEBUG counters->llxSuccess->inc(tid);
//            if (scx1 != dummy) shmem->unprotect(tid, scx1);
            // on x86/64, we do not need any memory barrier here to prevent mutable fields of node from being moved before our read of scx1, because the hardware does not perform read-read reordering. on another platform, we would need to ensure no read from after this point is reordered before this point (technically, before the read that becomes scx1)...
            return scx1;    // success
        } else {
            DEBUG {
                IF_FAIL_TO_PROTECT_SCX(info, tid, scx2, &node->scxRecord, &node->marked) {
                    TRACE COUTATOMICTID("llx return1.b (tid="<<tid<<" key="<<node->key<<")\n");
                    DEBUG counters->llxFail->inc(tid);
                    return NULL;
                } else {
                    assert(scx1 == dummy || shmem->isProtected(tid, scx1));
                    assert(shmem->isProtected(tid, scx2));
                    assert(node == root || shmem->isProtected(tid, node));
                    int __state = scx2->state;
                    SCXRecord<K,V>* __scx = node->scxRecord;
                    if (!IS_VERSION_NUMBER(__scx) && (marked && __state & 2 && __scx == scx2)) {
                        COUTATOMICTID("ERROR: marked && state aborted! raising signal SIGTERM..."<<endl);
                        COUTATOMICTID("node      = "<<*node<<endl);
                        COUTATOMICTID("scx2      = "<<*scx2<<endl);
                        COUTATOMICTID("state     = "<<state<<" bits="<<bitset<32>(state)<<endl);
                        COUTATOMICTID("marked    = "<<marked<<endl);
                        COUTATOMICTID("__state   = "<<__state<<" bits="<<bitset<32>(__state)<<endl);
                        assert(node->marked);
                        assert(scx2->state & 2 /* aborted */);
                        raise(SIGTERM);
                    }
//                    shmem->unprotect(tid, scx2);
                }
            }
            if (shmem->shouldHelp()) {
                IF_FAIL_TO_PROTECT_SCX(info, tid, scx2, &node->scxRecord, &node->marked) {
                    TRACE COUTATOMICTID("llx return3 (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<")\n");
                    DEBUG counters->llxFail->inc(tid);
                    return NULL;
                } // return and retry
                assert(scx2 != dummy);
                assert(shmem->isProtected(tid, scx2));
                TRACE COUTATOMICTID("llx help 1 tid="<<tid<<endl);
                if (!IS_VERSION_NUMBER(scx2)) {
                    help(tid, scx2, true);
                }
//                if (scx2 != dummy) shmem->unprotect(tid, scx2);
            }
        }
//        if (scx1 != dummy) shmem->unprotect(tid, scx1);
    } else if (state == SCXRecord<K,V>::STATE_INPROGRESS) {
        if (shmem->shouldHelp()) {
            assert(scx1 != dummy);
            assert(shmem->isProtected(tid, scx1));
            TRACE COUTATOMICTID("llx help 2 tid="<<tid<<endl);
            if (!IS_VERSION_NUMBER(scx1)) {
                help(tid, scx1, true);
            }
        }
//        if (scx1 != dummy) shmem->unprotect(tid, scx1);
    } else {
        // state committed and marked
        assert(state == 1); /* SCXRecord<K,V>::STATE_COMMITTED */
        assert(marked);
        if (shmem->shouldHelp()) {
            SCXRecord<K,V> *scx3 = node->scxRecord;
            if (scx3 == dummy) {
                COUTATOMICTID("scx1="<<scx1<<endl);
                COUTATOMICTID("scx3="<<scx3<<endl);
                COUTATOMICTID("dummy="<<dummy<<endl);
                COUTATOMICTID("node="<<*node<<endl);
            }
//            if (scx1 != dummy) shmem->unprotect(tid, scx1);
            IF_FAIL_TO_PROTECT_SCX(info, tid, scx3, &node->scxRecord, &node->marked) {
                TRACE COUTATOMICTID("llx return4 (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<")\n");
                DEBUG counters->llxFail->inc(tid);
                return NULL;
            } // return and retry
            assert(scx3 != dummy);
            assert(shmem->isProtected(tid, scx3));
            TRACE COUTATOMICTID("llx help 3 tid="<<tid<<endl);
            if (!IS_VERSION_NUMBER(scx3)) {
                help(tid, scx3, true);
            }
//            if (scx3 != dummy) shmem->unprotect(tid, scx3);
        } else {
//            if (scx1 != dummy) shmem->unprotect(tid, scx1);
        }
    }
    TRACE COUTATOMICTID("llx return5 (tid="<<tid<<" state="<<state<<" marked="<<marked<<" key="<<node->key<<")\n");
    DEBUG counters->llxFail->inc(tid);
    return NULL;            // fail
}
