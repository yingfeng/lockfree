/**
 * Preliminary C++ implementation of binary search tree using LLX/SCX.
 * 
 * Copyright (C) 2014 Trevor Brown
 * This preliminary implementation is CONFIDENTIAL and may not be distributed.
 */

#ifndef NODE_H
#define	NODE_H

#include <iostream>
#include <iomanip>
#include <atomic>
#include <set>
#include "scxrecord.h"
#ifdef USE_RECLAIMER_RCU
#include <urcu.h>
#define RECLAIM_RCU_RCUHEAD_DEFN struct rcu_head rcuHeadField
#else
#define RECLAIM_RCU_RCUHEAD_DEFN 
#endif
using namespace std;

template <class K, class V>
class Node {
public:
//    int weight;
    V value;
    K key;
    atomic_uintptr_t scxRecord;
    atomic_bool marked; // might be able to combine this elegantly with scx record pointer... (maybe we can piggyback on the version number mechanism, using the same bit to indicate ver# OR marked)
    atomic_uintptr_t left;
    atomic_uintptr_t right;
    RECLAIM_RCU_RCUHEAD_DEFN;
//    char padding[24+64];
//    char padding[10000];
    
    Node() {
        // left blank for efficiency with custom allocator
    }
    Node(const Node& node) {
        // left blank for efficiency with custom allocator
    }

    K getKey() { return key; }
    V getValue() { return value; }
    
    friend ostream& operator<<(ostream& os, const Node<K,V>& obj) {
        ios::fmtflags f( os.flags() );
        os<<"[key="<<obj.key
//          <<" weight="<<obj.weight
          <<" marked="<<obj.marked.load();
        os<<" scxRecord@0x"<<hex<<(long)(obj.scxRecord.load());
//        os.flags(f);
        os<<" left@0x"<<hex<<(long)(obj.left.load());
//        os.flags(f);
        os<<" right@0x"<<hex<<(long)(obj.right.load());
//        os.flags(f);
        os<<"]"<<"@0x"<<hex<<(long)(&obj);
        os.flags(f);
        return os;
    }
    
    // somewhat slow version that detects cycles in the tree
    void printTreeFile(ostream& os, set< Node<K,V>* > *seen) {
//        os<<"(["<<key<<","<</*(long)(*this)<<","<<*/marked<<","<<scxRecord->state<<"],"<<weight<<",";
        os<<"(["<<key<<","<<marked.load()<<"],"<<((SCXRecord<K,V>*) scxRecord.load())->state.load()<<",";
        Node<K,V>* __left = (Node<K,V>*) left.load();
        Node<K,V>* __right = (Node<K,V>*) right.load();
        if (__left == NULL) {
            os<<"-";
        } else if (seen->find(__left) != seen->end()) {   // for finding cycles
            os<<"!"; // cycle!                          // for finding cycles
        } else {
            seen->insert(__left);
            __left->printTreeFile(os, seen);
        }
        os<<",";
        if (__right == NULL) {
            os<<"-";
        } else if (seen->find(__right) != seen->end()) {  // for finding cycles
            os<<"!"; // cycle!                          // for finding cycles
        } else {
            seen->insert(__right);
            __right->printTreeFile(os, seen);
        }
        os<<")";
    }

    void printTreeFile(ostream& os) {
        set< Node<K,V>* > seen;
        printTreeFile(os, &seen);
    }
    
    // somewhat slow version that detects cycles in the tree
    void printTreeFileWeight(ostream& os, set< Node<K,V>* > *seen) {
//        os<<"(["<<key<<","<</*(long)(*this)<<","<<*/marked<<","<<scxRecord->state<<"],"<<weight<<",";
        os<<"(["<<key<<"],";//<<weight<<",";
        Node<K,V>* __left = (Node<K,V>*) left.load();
        Node<K,V>* __right = (Node<K,V>*) right.load();
        if (__left == NULL) {
            os<<"-";
        } else if (seen->find(__left) != seen->end()) {   // for finding cycles
            os<<"!"; // cycle!                          // for finding cycles
        } else {
            seen->insert(__left);
            __left->printTreeFileWeight(os, seen);
        }
        os<<",";
        if (__right == NULL) {
            os<<"-";
        } else if (seen->find(__right) != seen->end()) {  // for finding cycles
            os<<"!"; // cycle!                          // for finding cycles
        } else {
            seen->insert(__right);
            __right->printTreeFileWeight(os, seen);
        }
        os<<")";
    }

    void printTreeFileWeight(ostream& os) {
        set< Node<K,V>* > seen;
        printTreeFileWeight(os, &seen);
    }

};

#endif	/* NODE_H */

