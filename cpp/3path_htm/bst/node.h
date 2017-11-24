/**
 * Fast HTM-based data structures using 3-paths.
 * 
 * Copyright (C) 2014 Trevor Brown
 * 
 */

#ifndef NODE_H
#define	NODE_H

#include <iostream>
#include <iomanip>
#include <atomic>
#include <set>
#include "scxrecord.h"
using namespace std;

template <class K, class V>
class Node {
public:
//    int weight;
    V value;
    K key;
    SCXRecord<K,V> * volatile scxRecord;
    volatile bool marked;
    Node<K,V> * volatile left;
    Node<K,V> * volatile right;
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
          <<" marked="<<obj.marked;
        os<<" scxRecord@0x"<<hex<<(long)(obj.scxRecord);
//        os.flags(f);
        os<<" left@0x"<<hex<<(long)(obj.left);
//        os.flags(f);
        os<<" right@0x"<<hex<<(long)(obj.right);
//        os.flags(f);
        os<<"]"<<"@0x"<<hex<<(long)(&obj);
        os.flags(f);
        return os;
    }
    
    // somewhat slow version that detects cycles in the tree
    void printTreeFile(ostream& os, set< Node<K,V>* > *seen) {
//        os<<"(["<<key<<","<</*(long)(*this)<<","<<*/marked<<","<<scxRecord->state<<"],"<<weight<<",";
        os<<"(["<<key<<","<<marked<<"],"<<scxRecord->state<<",";
        if (left == NULL) {
            os<<"-";
        } else if (seen->find((Node<K,V> *)left) != seen->end()) {   // for finding cycles
            os<<"!"; // cycle!                          // for finding cycles
        } else {
            seen->insert((Node<K,V> *) left);
            left->printTreeFile(os, seen);
        }
        os<<",";
        if (right == NULL) {
            os<<"-";
        } else if (seen->find((Node<K,V> *)right) != seen->end()) {  // for finding cycles
            os<<"!"; // cycle!                          // for finding cycles
        } else {
            seen->insert((Node<K,V> *)right);
            right->printTreeFile(os, seen);
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
        if (left == NULL) {
            os<<"-";
        } else if (seen->find((Node<K,V> *)left) != seen->end()) {   // for finding cycles
            os<<"!"; // cycle!                          // for finding cycles
        } else {
            seen->insert((Node<K,V> *)left);
            left->printTreeFileWeight(os, seen);
        }
        os<<",";
        if (right == NULL) {
            os<<"-";
        } else if (seen->find((Node<K,V> *)right) != seen->end()) {  // for finding cycles
            os<<"!"; // cycle!                          // for finding cycles
        } else {
            seen->insert((Node<K,V> *)right);
            right->printTreeFileWeight(os, seen);
        }
        os<<")";
    }

    void printTreeFileWeight(ostream& os) {
        set< Node<K,V>* > seen;
        printTreeFileWeight(os, &seen);
    }

};

#endif	/* NODE_H */

