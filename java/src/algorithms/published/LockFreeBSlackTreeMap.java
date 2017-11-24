/**
 * Implementation of the dictionary ADT with a lock-free B-slack tree.
 * Copyright (C) 2014 Trevor Brown
 * Contact (me [at] tbrown [dot] pro) with questions or comments.
 *
 * Details of the B-slack tree algorithm appear in the paper:
 *    Brown, Trevor. B-slack trees: space efficient B-trees. SWAT 2014.
 * 
 * Unfortunately, in Java, you cannot embed arrays inside nodes, so nodes
 * contain pointers to key/value arrays. This makes the tree somewhat less
 * efficient in Java. This is a reference implementation intended to show how
 * to produce a simple implementation of a B-slack tree. The intention is to
 * provide others with a guide they can use to produce a simple B-slack tree
 * implementation in a language that gives more control over memory layout,
 * such as C or C++. Better implementations are possible. See the full version
 * of the paper for discussion of the algorithm and implementation details.
 * 
 * The paper leaves it up to the implementer to decide when and how to perform
 * rebalancing steps (i.e., Root-Zero, Root-Replace, Absorb, Split, Compress
 * and One-Child). In this implementation, we keep track of violations and fix
 * them using a recursive cleanup procedure, which is designed as follows.
 * After performing a rebalancing step that replaced a set R of nodes,
 * recursive invocations are made for every violation that appears at a newly
 * created node. Thus, any violations that were present at nodes in R are either
 * eliminated by the rebalancing step, or will be fixed by recursive calls.
 * This way, if an invocation I of this cleanup procedure is trying to fix a
 * violation at a node that has been replaced by another invocation I' of cleanup,
 * then I can hand off responsibility for fixing the violation to I'.
 * Designing the rebalancing procedure to allow responsibility to be handed
 * off in this manner is not difficult; it simply requires going through each
 * rebalancing step S and determining which nodes involved in S can have
 * violations after S (and then making a recursive call for each violation).
 * 
 * Updated Nov 10, 2016.
 * 
 * -----------------------------------------------------------------------------
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

package algorithms.published;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.concurrent.atomic.AtomicReferenceArray;
import java.util.concurrent.atomic.AtomicReferenceFieldUpdater;
import main.Tests;
import main.support.KSTNode;

/**
 *
 * @author Trevor Brown
 */
public class LockFreeBSlackTreeMap<K extends Comparable<? super K>,V> {

    // the following boolean determines whether the optimization to guarantee
    // amortized constant rebalancing (at the cost of decreasing average degree
    // by at most one) is used.
    // if it is false, then an amortized logarithmic number of rebalancing steps
    // may be performed per operation, but average degree increases slightly.
    private final boolean ALLOW_ONE_EXTRA_SLACK_PER_NODE = true;

    public final Comparator<? super K> comparator;
    public final int b;
    private final Node root;
    private final static SCXRecord DUMMY = new SCXRecord();
    private final static SCXRecord FINALIZED = new SCXRecord();
    private final static SCXRecord FAILED = new SCXRecord();
    private final AtomicReferenceFieldUpdater<LockFreeBSlackTreeMap.Node, LockFreeBSlackTreeMap.SCXRecord> updateScxPtr;
    
    // the following two variables are only useful for single threaded execution
    private final boolean DRAW_TREES_FOR_EACH_OPERATION = false;
    private final boolean SEQUENTIAL_STAT_TRACKING = false;

    // these variables are only used by single threaded executions,
    // and only if SEQUENTIAL_STAT_TRACKING == true.
    // they simply track various events in the b-slack tree.
    private int operationCount = 0;
    private int overflows = 0;
    private int weightChecks = 0;
    private int weightCheckSearches = 0;
    private int weightFixAttempts = 0;
    private int weightFixes = 0;
    private int weightEliminated = 0;
    private int slackChecks = 0;
    private int slackCheckTotaling = 0;
    private int slackCheckSearches = 0;
    private int slackFixTotaling = 0;
    private int slackFixAttempts = 0;
    private int slackFixSCX = 0;
    private int slackFixes = 0;
    
    public void debugPrint() {
        if (SEQUENTIAL_STAT_TRACKING) {
            System.err.println("overflows="+overflows);
            System.err.println("weightChecks="+weightChecks);
            System.err.println("weightCheckSearches="+weightCheckSearches);
            System.err.println("weightFixAttempts="+weightFixAttempts);
            System.err.println("weightFixes="+weightFixes);
            System.err.println("weightEliminated="+weightEliminated);
            System.err.println("slackChecks="+slackChecks);
            System.err.println("slackCheckTotaling="+slackCheckTotaling);
            System.err.println("slackCheckSearches="+slackCheckSearches);
            System.err.println("slackFixTotaling="+slackFixTotaling);
            System.err.println("slackFixAttempts="+slackFixAttempts);
            System.err.println("slackFixSCX="+slackFixSCX);
            System.err.println("slackFixes="+slackFixes);
        }
        System.err.println("averageDegree="+getAverageDegree());
        System.err.println("averageDepth="+getAverageKeyDepth());
        System.err.println("height="+getHeight());
        System.err.println("internalNodes="+getNumberOfInternals());
        System.err.println("leafNodes="+getNumberOfLeaves());
        System.err.println("data structure validation="+isBSlackTree());
    }
    
    /**
     * Creates a new B-slack tree wherein: <br>
     *      each internal node has up to 16 child pointers, and <br>
     *      each leaf has up to 16 key/value pairs.
     */
    public LockFreeBSlackTreeMap(final K anyKey) {
        this(16, anyKey);
    }
    
    /**
     * Creates a new B-slack tree wherein: <br>
     *      each internal node has up to <code>nodeCapacity</code> child pointers, and <br>
     *      each leaf has up to <code>nodeCapacity</code> key/value pairs.
     */
    public LockFreeBSlackTreeMap(final int nodeCapacity, final K anyKey) {
        this(nodeCapacity, null, anyKey);
    }
    
    /**
     * Creates a new B-slack tree wherein: <br>
     *      each internal node has up to <code>nodeCapacity</code> child pointers, and <br>
     *      each leaf has up to <code>nodeCapacity</code> key/value pairs, and <br>
     *      keys are ordered according to the provided comparator.
     */
    public LockFreeBSlackTreeMap(final int nodeCapacity, final Comparator comparator, final K anyKey) {
        this.b = nodeCapacity;
        this.comparator = comparator;
        updateScxPtr = AtomicReferenceFieldUpdater.newUpdater(Node.class, SCXRecord.class, "scxPtr");
        // initial tree: root is a sentinel node (with one pointer and no keys)
        //               that points to an empty node (no pointers and no keys)
        Node rootLeft = new Node(new Object[0], new Object[0], null, true, anyKey);
        this.root = new Node(new Object[0], null, new AtomicReferenceArray<>((Node[]) new Node[]{rootLeft}), true, anyKey);
//        System.out.println("root = " + root);
//        System.out.println("rootLeft = " + rootLeft);
    }
    
    private int sequentialSize(Node node) {
        if (node.isLeaf()) {
            return node.keys.length;
        }
        int retval = 0;
        for (int i=0;i<node.children.length();++i) {
            Node child = node.children.get(i);
            retval += sequentialSize(child);
        }
        return retval;
    }
    
    public int sequentialSize() {
        return sequentialSize(root.children.get(0));
    }
    
    public final KSTNode<K> getRoot() {
        return getRoot(root);
    }
    private KSTNode<K> getRoot(Node node) {
        if (node == null) return null;
        ArrayList<K> keys = new ArrayList<>();
        if (node.keys != null) {
            for (int i=0;i<node.keys.length;i++) {
                keys.add((K) node.keys[i]);
            }
        }
        
        if (node.isLeaf()) {
            return new KSTNode<>(
                keys,
                keys.size(),
                (node.weight ? 1 : 0),
                Integer.toHexString(node.hashCode()));
        } else {
            KSTNode[] children = new KSTNode[node.children.length()];
            for (int i=0;i<node.children.length();++i) {
                children[i] = getRoot(node.children.get(i));
            }
            return new KSTNode<>(keys,
                                  keys.size(),
                                  (node.weight ? 1 : 0),
                                  Integer.toHexString(node.hashCode()),
                                  (KSTNode<K>[]) children);
        }
    }
    
    
    /**
     * Returns true if the dictionary contains key and false otherwise.
     */
    public boolean containsKey(final K key) {
        return get(key) != null;
    }
    
    /**
     * Returns the value associated with key, or null if key is not in the
     * dictionary.
     */
    public V get(final K key) {
        final Comparable<? super K> k = comparable(key);
        Node l = root.children.get(0);
        while (true) {
            if (l.isLeaf()) break;
            l = l.children.get(l.getChildIndex(k));
        }
        final int keyIndex = l.getKeyIndex(k);
        return (l.containsKey(keyIndex)) ? (V) l.values[keyIndex] : null;
    }
    
    private void printTree() {
        //Tests.renderTree((KSTNode<Integer>) getRoot(), "exception", true);
    }
    
    /**
     * Inserts a key/value pair into the dictionary if key is not already
     * present, and does not change the data structure otherwise.
     * Precondition: key != null
     * 
     * @param key key to insert into the dictionary
     * @param value value to associate with key
     * @return true if the key/value pair was inserted into the dictionary,
     *         and false if key was already in the dictionary.
     */
    public boolean putIfAbsent(final K key, final V value) {
        return doPut(key, value, false) == null;
    }
    /**
     * Inserts a key/value pair into the dictionary, replacing any previous
     * value associated with the key.
     * Precondition: key != null
     * 
     * @param key key to insert into the dictionary
     * @param value value to associate with key
     * @return the value associated with key just before this operation, and
     *         null if key was not previously in the dictionary.
     */
    public V put(final K key, final V value) {
        try {
            return doPut(key, value, true);
        } catch (Exception ex) { ex.printStackTrace(); printTree(); System.exit(-1); }
        return null; // unreachable statement
    }
    private V doPut(final K key, final V value, final boolean replace) {
        while (true) {
            /**
             * search.
             */
            final Comparable<? super K> k = comparable(key);
            Node gp = null;
            Node p = root;
            Node l = p.children.get(0);
            int ixToP = -1;
            int ixToL = 0;
            while (!l.isLeaf()) {
                ixToP = ixToL;
                ixToL = l.getChildIndex(k);
                gp = p;
                p = l;
                l = l.children.get(ixToL);
            }

            /**
             * do the update.
             */
            int keyIndex = l.getKeyIndex(k);
            if (l.containsKey(keyIndex)) {
                /**
                 * if l already contains key, replace the existing value.
                 */
                final V oldValue = (V) l.values[keyIndex];
                if (!replace) {
                    return oldValue;
                }

                final SCXRecord[] ops = new SCXRecord[1];
                final Node[] nodes = new Node[]{null, l};

                if (!LLX(p, null, 0, ops, nodes) || p.children.get(ixToL) != l) continue;

                final Object[] keys = new Object[l.keys.length];
                final Object[] values = new Object[l.values.length];
                System.arraycopy(l.keys, 0, keys, 0, l.keys.length);
                System.arraycopy(l.values, 0, values, 0, l.values.length);
                values[keyIndex] = value;
                assert(keys.length > 0);
                Node newL = new Node(keys, values, null, l.weight, keys[0]);

                SCXRecord scxPtr = new SCXRecord(nodes, ops, newL, ixToL);
//                System.out.println("replace: newL.keys.length = " + newL.keys.length);

                if (helpSCX(scxPtr, 0)) {
                    if (DRAW_TREES_FOR_EACH_OPERATION) Tests.renderTree((KSTNode<Integer>) getRoot(), "after-op" + (++operationCount) + "-replace-"+key, true);
                    return oldValue;
                }

            } else {
                /**
                 * if l does not contain key, we have to insert it.
                 */
                
                final SCXRecord[] ops = new SCXRecord[1];
                final Node[] nodes = new Node[]{null, l};

                if (!LLX(p, null, 0, ops, nodes) || p.children.get(ixToL) != l) continue;

                // we start by creating a sorted array containing key and all of l's existing keys
                // (and likewise for the values)
                keyIndex = -(keyIndex + 1); // 
                final Object[] keys = new Object[l.keys.length+1];
                final Object[] values = new Object[l.keys.length+1];
                System.arraycopy(l.keys, 0, keys, 0, keyIndex);
                System.arraycopy(l.keys, keyIndex, keys, keyIndex+1, l.keys.length-keyIndex);
                keys[keyIndex] = key;
                System.arraycopy(l.values, 0, values, 0, keyIndex);
                System.arraycopy(l.values, keyIndex, values, keyIndex+1, l.values.length-keyIndex);
                values[keyIndex] = value;

                if (l.keys.length < b) {
                    /**
                     * Insert.
                     */
                    // the new arrays are small enough to fit in a single node,
                    // so we replace l by a new node containing these arrays.
                    assert(keys.length > 0);
                    Node newL = new Node(keys, values, null, true, keys[0]);
                    SCXRecord scxPtr = new SCXRecord(nodes, ops, newL, ixToL);
//                    System.out.println("insert: newL.keys.length = " + newL.keys.length);
                    if (helpSCX(scxPtr, 0)) {
                        if (DRAW_TREES_FOR_EACH_OPERATION) Tests.renderTree((KSTNode<Integer>) getRoot(), "after-op" + (++operationCount) + "-insert-"+key, true);
                        return null;
                    }

                } else {
                    /**
                     * Overflow.
                     */
                    // the new arrays are too big to fit in a single node,
                    // so we replace l by a new subtree containing three new nodes:
                    // a parent, and two leaves.
                    // the new arrays are then split between the two new leaves.
                    final int size1 = keys.length/2;
                    final Object[] keys1 = new Object[size1];
                    final Object[] values1 = new Object[size1];
                    System.arraycopy(keys, 0, keys1, 0, size1);
                    System.arraycopy(values, 0, values1, 0, size1);
                    final int size2 = keys.length - size1;
                    final Object[] keys2 = new Object[size2];
                    final Object[] values2 = new Object[size2];
                    System.arraycopy(keys, size1, keys2, 0, size2);
                    System.arraycopy(values, size1, values2, 0, size2);
                    assert(keys1.length > 0);
                    assert(keys2.length > 0);
                    final Node newL = new Node(
                            new Object[]{keys2[0]},
                            null,
                            new AtomicReferenceArray<>((Node[]) new Node[]{
                                new Node(keys1, values1, null, true, keys1[0]),
                                new Node(keys2, values2, null, true, keys2[0])
                            }),
                            p == root,
                            keys2[0]);
                    // note: weight of new internal node newL will be zero,
                    //       unless it is the root. this is because we test
                    //       gp == null, above. in doing this, we are actually
                    //       performing Root-Zero at the same time as this Overflow
                    //       if newL will become the root.
                    SCXRecord scxPtr = new SCXRecord(nodes, ops, newL, ixToL);
//                    System.out.println("overflow: newL.keys.length = " + newL.keys.length);
                    if (helpSCX(scxPtr, 0)) {
                        if (DRAW_TREES_FOR_EACH_OPERATION) Tests.renderTree((KSTNode<Integer>) getRoot(), "after-op" + (++operationCount) + "-overflow-"+key, true);
                        if (SEQUENTIAL_STAT_TRACKING) ++overflows;
                        
                        // after overflow, there may be a weight violation at newL,
                        // and there may be a slack violation at p
                        fixWeightViolation(newL);
                        fixDegreeOrSlackViolation(p);
                        return null;
                    }
                }
            }
        }
    }
    
    /**
     * Removes a key from the dictionary (eliminating any value associated with
     * it), and returns the value that was associated with the key just before
     * the key was removed.
     * Precondition: key != null
     * 
     * @param key the key to remove from the dictionary
     * @return the value associated with the key just before key was removed
     */
    public V remove(final K key) {
        try {
        while (true) {
            /**
             * search.
             */
            final Comparable<? super K> k = comparable(key);
            Node gp = null;
            Node p = root;
            Node l = p.children.get(0);
            int ixToP = -1;
            int ixToL = 0;
            while (!l.isLeaf()) {
                ixToP = ixToL;
                ixToL = l.getChildIndex(k);
                gp = p;
                p = l;
                l = l.children.get(ixToL);
            }

            /**
             * do the update.
             */
            final int keyIndex = l.getKeyIndex(k);
            if (!l.containsKey(keyIndex)) {
                /**
                 * if l does not contain key, we are done.
                 */
                return null;
            } else {
                /**
                 * if l contains key, replace l by a new copy that does not contain key.
                 */
                
                final SCXRecord[] ops = new SCXRecord[1];
                final Node[] nodes = new Node[]{null, l};
                
                if (!LLX(p, null, 0, ops, nodes) || p.children.get(ixToL) != l) continue;

                final V oldValue = (V) l.values[keyIndex];
                final Object[] keys = new Object[l.keys.length-1];
                final Object[] values = new Object[l.keys.length-1];
                System.arraycopy(l.keys, 0, keys, 0, keyIndex);
                System.arraycopy(l.keys, keyIndex+1, keys, keyIndex, keys.length-keyIndex);
                System.arraycopy(l.values, 0, values, 0, keyIndex);
                System.arraycopy(l.values, keyIndex+1, values, keyIndex, values.length-keyIndex);

                Node newL = new Node(keys, values, null, true, l.keys[0]); // NOTE: l.keys[0] MIGHT BE DELETED, IN WHICH CASE newL IS EMPTY. HOWEVER, newL CAN STILL BE LOCATED BY SEARCHING FOR l.keys[0], SO WE USE THAT AS THE searchKey FOR newL.
                SCXRecord scxPtr = new SCXRecord(nodes, ops, newL, ixToL);
//                System.out.println("remove: newL.keys.length = " + newL.keys.length);
                if (helpSCX(scxPtr, 0)) {
                    if (DRAW_TREES_FOR_EACH_OPERATION) Tests.renderTree((KSTNode<Integer>) getRoot(), "after-op" + (++operationCount) + "-remove-"+key, true);
                    /**
                     * Compress may be needed at p after removing key from l.
                     */
                    fixDegreeOrSlackViolation(p);
                    return oldValue;
                }
            }
        }
        } catch (Exception ex) { ex.printStackTrace(); printTree(); System.exit(-1); }
        return null; // unreachable statement
    }
    
    private Comparable comparable(final Object key) {
        if (key == null) {
            throw new NullPointerException();
        }
        if (comparator == null) {
            return (Comparable) key;
        }
        return new Comparable() {
            final Comparator _cmp = comparator;

            @SuppressWarnings("unchecked")
            public int compareTo(final Object rhs) { return _cmp.compare(key, rhs); }
        };
    }
        
    /**
     *  suppose there is a violation at node that is replaced by an update (specifically, a template operation that performs a successful scx).
     *  we want to hand off the violation to the update that replaced the node.
     *  so, for each update, we consider all the violations that could be present before the update, and determine where each violation can be moved by the update.
     *
     *  in the following we use several names to refer to nodes involved in the update:
     *    n is the topmost new node created by the update,
     *    leaf is the leaf replaced by the update,
     *    u is the node labeled in the figure showing the bslack updates in the paper,
     *    pi(u) is the parent of u,
     *    p is the node whose child pointer is changed by the update (and the parent of n after the update), and
     *    root refers to the root of the bslack tree (NOT the sentinel root -- in this implementation it refers to root.children.get(0))
     *
     *  delete [check: slack@p]
     *      no weight at leaf
     *      no degree at leaf
     *      no slack at leaf
     *      [maybe create slack at p]
     *
     *  insert [check: none]
     *      no weight at leaf
     *      no degree at leaf
     *      no slack at leaf
     *
     *  overflow [check: weight@n, slack@p]
     *      no weight at leaf
     *      no degree at leaf
     *      no slack at leaf
     *      [create weight at n]
     *      [maybe create slack at p]
     *
     *  root-zero [check: degree@n, slack@n]
     *      weight at root -> eliminated
     *      degree at root -> degree at n
     *      slack at root -> slack at n
     *
     *  root-replace [check: degree@n, slack@n]
     *      no weight at root
     *      degree at root -> eliminated
     *      slack at root -> eliminated
     *      weight at child of root -> eliminated
     *      degree at child of root -> degree at n
     *      slack at child of root -> slack at n
     *
     *  absorb [check: slack@n]
     *      no weight at pi(u)
     *      degree at pi(u) -> eliminated
     *      slack at pi(u) -> eliminated or slack at n
     *      weight at u -> eliminated
     *      no degree at u
     *      slack at u -> slack at n
     *
     *  split [check: weight@n, slack@n, slack@n.p1, slack@n.p2, slack@p]
     *      no weight at pi(u)
     *      no degree at pi(u)
     *      slack at pi(u) and/or u -> slack at n and/or n.p1 and/or n.p2
     *      weight at u -> weight at n
     *      no degree at u (since u has exactly 2 pointers)
     *      [maybe create slack at p]
     *
     *  compress [check: slack@children(n), slack@p, degree@n]
     *      no weight at u
     *      no degree at u
     *      slack at u -> eliminated
     *      no weight at any child of u
     *      degree at a child of u -> eliminated
     *      slack at a child of u -> eliminated or slack at a child of n
     *      [maybe create slack at any or all children of n]
     *      [maybe create slack at p]
     *      [maybe create degree at n]
     *
     *  one-child [check: slack@children(n)]
     *      no weight at u
     *      degree at u -> eliminated
     *      slack at u -> eliminated or slack at a child of n
     *      no weight any sibling of u or pi(u)
     *      no degree at any sibling of u or pi(u)
     *      slack at a sibling of u -> eliminated or slack at a child of n
     *      no slack at pi(u)
     *      [maybe create slack at any or all children of n]
     */
    
    // returns true if the invocation of this method
    // (and not another invocation of a method performed by this method)
    // performed an scx, and false otherwise
    private boolean fixWeightViolation(final Node viol) {
        if (SEQUENTIAL_STAT_TRACKING) ++weightChecks;
        if (viol.weight) return false;

        // assert: viol is internal (because leaves always have weight = 1)
        // assert: viol is not root (because the root always has weight = 1)

        // do an optimistic check to see if viol was already removed from the tree
        if (LLX(viol, null) == FINALIZED) {
            // recall that nodes are finalized precisely when
            // they are removed from the tree
            // we hand off responsibility for any violations at viol to the
            // process that removed it.
            return false;
        }

        // try to locate viol, and fix any weight violation at viol
        while (true) {
            if (SEQUENTIAL_STAT_TRACKING) ++weightCheckSearches;
            final Comparable<? super K> k = comparable(viol.searchKey);
            Node gp = null;
            Node p = root;
            Node l = p.children.get(0);
            int ixToP = -1;
            int ixToL = 0;
            while (!l.isLeaf() && l != viol) {
                ixToP = ixToL;
                ixToL = l.getChildIndex(k);
                gp = p;
                p = l;
                l = l.children.get(ixToL);
            }

            if (l != viol) {
                // l was replaced by another update.
                // we hand over responsibility for viol to that update.
                return false;
            }
            if (SEQUENTIAL_STAT_TRACKING) ++weightFixAttempts;
            
            // we cannot apply this update if p has a weight violation
            // so, we check if this is the case, and, if so, try to fix it
            if (!p.weight) {
                fixWeightViolation(p);
                continue;
            }

            final SCXRecord[] ops = new SCXRecord[3];
            final Node[] nodes = new Node[3];

            if (!LLX(gp, null, 0, ops, nodes) || gp.children.get(ixToP) != p) continue; // retry the search
            if (!LLX(p, null, 1, ops, nodes) || p.children.get(ixToL) != l) continue; // retry the search
            if (!LLX(l, null, 2, ops, nodes)) continue; // retry the search

            // merge keys of p and l into one big array (and similarly for children)
            // (we essentially replace the pointer to l with the contents of l)
            final int c = p.children.length() + l.children.length();
            final int size = c-1;
            final K[] keys = (K[]) new Comparable[size-1];
            final Node[] children = (Node[]) new Node[size];
            arraycopy(p.children, 0, children, 0, ixToL);
            arraycopy(l.children, 0, children, ixToL, l.children.length());
            arraycopy(p.children, ixToL+1, children, ixToL+l.children.length(), p.children.length()-(ixToL+1));
            System.arraycopy(p.keys, 0, keys, 0, ixToL);
            System.arraycopy(l.keys, 0, keys, ixToL, l.keys.length);
            System.arraycopy(p.keys, ixToL, keys, ixToL+l.keys.length, p.keys.length-ixToL);

            if (size <= b) {
                /**
                 * Absorb
                 */
                // the new arrays are small enough to fit in a single node,
                // so we replace p by a new internal node.
                assert(keys.length > 0);
                final Node newP = new Node(keys, null, new AtomicReferenceArray<>(children), true, keys[0]);
                SCXRecord scxPtr = new SCXRecord(nodes, ops, newP, ixToP);
                if (helpSCX(scxPtr, 0)) {
                    if (DRAW_TREES_FOR_EACH_OPERATION) Tests.renderTree((KSTNode<Integer>) getRoot(), "after-op" + (++operationCount) + "-absorb-"+viol.searchKey, true);
                    if (SEQUENTIAL_STAT_TRACKING) ++weightFixes;
                    if (SEQUENTIAL_STAT_TRACKING) ++weightEliminated;
                    
                    //    absorb [check: slack@n]
                    //        no weight at pi(u)
                    //        degree at pi(u) -> eliminated
                    //        slack at pi(u) -> eliminated or slack at n
                    //        weight at u -> eliminated
                    //        no degree at u
                    //        slack at u -> slack at n

                    /**
                     * Compress may be needed at the new internal node we created
                     * (since we move grandchildren from two parents together).
                     */
                    fixDegreeOrSlackViolation(newP);
                    return true;
                }

            } else {
                /**
                 * Split
                 */
                // the new arrays are too big to fit in a single node,
                // so we replace p by a new internal node and two new children.

                // we take the big merged array and split it into two arrays,
                // which are used to create two new children u and v.
                // we then create a new internal node (whose weight will be zero
                // if it is not the root), with u and v as its children.
                final int size1 = size / 2;
                final int size2 = size - size1;
                final K[] keys1 = (K[]) new Comparable[size1-1];
                final K[] keys2 = (K[]) new Comparable[size2-1];
                final Node[] children1 = (Node[]) new Node[size1];
                final Node[] children2 = (Node[]) new Node[size2];
                System.arraycopy(keys, 0, keys1, 0, size1-1);
                System.arraycopy(keys, size1, keys2, 0, size2-1);
                System.arraycopy(children, 0, children1, 0, size1);
                System.arraycopy(children, size1, children2, 0, size2);
                assert(size1 > 0);
                assert(size2 > 0);
                final Node left = new Node(keys1, null, new AtomicReferenceArray<>(children1), true, keys1[0]);
                final Node right = new Node(keys2, null, new AtomicReferenceArray<>(children2), true, keys2[0]);
                final Node newP = new Node(
                        new Comparable[]{keys[size1-1]},
                        null,
                        new AtomicReferenceArray<>((Node[]) new Node[]{left, right}),
                        gp == root,
                        keys[size1-1]);

                SCXRecord scxPtr = new SCXRecord(nodes, ops, newP, ixToP);
                if (helpSCX(scxPtr, 0)) {
                    if (DRAW_TREES_FOR_EACH_OPERATION) Tests.renderTree((KSTNode<Integer>) getRoot(), "after-op" + (++operationCount) + "-split-"+viol.searchKey, true);
                    if (SEQUENTIAL_STAT_TRACKING) ++weightFixes;
                    if (SEQUENTIAL_STAT_TRACKING) if (gp == root) ++weightEliminated;

                    //    split [check: weight@n, slack@n, slack@n.p1, slack@n.p2, slack@p]
                    //        no weight at pi(u)
                    //        no degree at pi(u)
                    //        slack at pi(u) and/or u -> slack at n and/or n.p1 and/or n.p2
                    //        weight at u -> weight at n
                    //        no degree at u (since u has exactly 2 pointers)
                    //        [maybe create slack at p]

                    fixWeightViolation(newP);
                    fixDegreeOrSlackViolation(newP);    // corresponds to node n using the terminology of the preceding comment
                    fixDegreeOrSlackViolation(left);    // corresponds to node n.p1 using the terminology of the preceding comment
                    fixDegreeOrSlackViolation(right);   // corresponds to node n.p2 using the terminology of the preceding comment
                    fixDegreeOrSlackViolation(gp);      // corresponds to node p using the terminology of the preceding comment
                    return true;
                }
            }
        }
    }
    
    // returns true if the invocation of this method
    // (and not another invocation of a method performed by this method)
    // performed an scx, and false otherwise
    private boolean fixDegreeOrSlackViolation(final Node viol) {
        /**
         * at a high level, in this function, we search for viol,
         * and then try to fix any degree or slack violations that we see there.
         * it is possible that viol has already been removed from the tree,
         * in which case we hand off responsibility for any violations at viol
         * to the process that removed it from the tree.
         * however, searching to determine whether viol is in the tree
         * before we know whether there are any violations to fix is expensive.
         * so, we first do an optimistic check to see if there is a violation
         * to fix at viol. if not, we can stop.
         * however, if there is a violation, then we go ahead and do the search
         * to find viol and check whether it is still in the tree.
         * once we've determined that viol is in the tree,
         * we follow the tree update template to perform a rebalancing step.
         * of course, any violation at viol might have been fixed between when
         * we found the violation with our optimistic check,
         * and when our search arrived at viol.
         * so, as part of the template (specifically, the Conflict procedure),
         * after performing LLX on viol, we verify that there is still a
         * violation at viol. if not, then we are done.
         * if so, we use SCX to perform an update to fix the violation.
         * if that SCX succeeds, then a violation provably occurred at viol when
         * the SCX occurred.
         */
        
        // if viol is a leaf, then no violation occurs at viol
        if (SEQUENTIAL_STAT_TRACKING) ++slackChecks;
        if (viol.isLeaf()) return false;
        assert(viol.weight);
        
        // do an optimistic check to see if viol was already removed from the tree
        if (LLX(viol, null) == FINALIZED) {
            // recall that nodes are finalized precisely when
            // they are removed from the tree.
            // we hand off responsibility for any violations at viol to the
            // process that removed it.
            return false;
        }
        
        // optimistically check if there is no violation at viol before doing
        // a full search to try to locate viol and fix any violations at it
        if (viol.children.length() == 1) {
            // found a degree violation at viol
        } else {
            // note: to determine whether there is a slack violation at viol,
            //       we must look at all of the children of viol.
            //       we use LLX to get an atomic snapshot of the child pointers.
            //       if the LLX returns FINALIZED, then viol was removed from
            //       the tree, so we hand off responsibility for any violations
            //       at viol to the process that removed it.
            //       if the LLX returns FAILED, indicating that the LLX was
            //       concurrent with an SCX that changed, or will change, viol,
            //       then we abort the optimistic violation check.
            if (SEQUENTIAL_STAT_TRACKING) ++slackCheckTotaling;
            Node[] children = new Node[viol.children.length()];
            SCXRecord result = LLX(viol, children);
            if (result == FINALIZED) {
                // viol was removed from the tree, so we hand off responsibility
                // for any violations at viol to the process that removed it.
                return false;
            } else if (result == FAILED) {
                // llx failed: go ahead and do the full search to find viol
            } else {
                // we have a snapshot of the child pointers
                // determine whether there is a slack violation at viol
                int slack = 0;
                int numLeaves = 0;
                for (int i=0;i<children.length;++i) {
                    slack += b;
                    if (children[i].isLeaf()) {
                        ++numLeaves;
                        slack -= children[i].keys.length;
                    } else {
                        slack -= children[i].children.length();
                    }
                }
                if (numLeaves > 0 && numLeaves < children.length) {
                    // some children are internal and some are leaves
                    // consequently, there is a weight violation among the children.
                    // thus, we can't fix any degree or slack violation until
                    // the weight violation is fixed, and the rebalancing step
                    // that fixes it will replace that node, at which point
                    // that process will check for degree and slack violations.
                    // so, we hand off responsibility for any degree or slack
                    // violation at viol.
                    // note: that other process might actually be the current
                    // process, and the violation will be dealt with by a call
                    // somewhere higher up in the call stack.
                    return false;
                }
                if (slack >= b + (ALLOW_ONE_EXTRA_SLACK_PER_NODE ? children.length : 0)) {
                    // found a slack violation at viol
                } else {
                    // no slack violation or degree violation at viol
                    return false;
                }
            }
        }
        
        // we found a degree violation or slack violation at viol
        // note: it is easy/efficient to determine which type we found.
        //       if we found a degree violation above, then,
        //       since the number of children in a node does not change,
        //       viol will always satisfy viol.children.length() == 1.
        //       however, if viol.children.length() > 1,
        //       then we know we found a slack violation, above.

        // we search for viol and try to fix any violation we find there
        while (true) {
            if (SEQUENTIAL_STAT_TRACKING) ++slackCheckSearches;
            /**
             * search for viol
             */
            final Comparable<? super K> k = comparable(viol.searchKey);
            Node gp = null;
            Node p = root;
            Node l = p.children.get(0);
            int ixToP = -1;
            int ixToL = 0;
            while (!l.isLeaf() && l != viol) {
                ixToP = ixToL;
                ixToL = l.getChildIndex(k);
                gp = p;
                p = l;
                l = l.children.get(ixToL);
            }

            if (l != viol) {
                // l was replaced by another update.
                // we hand over responsibility for viol to that update.
                return false;
            }
            
            /**
             * observe that Compress and One-Child can be implemented in
             * exactly the same way.
             * consider the figure in the paper that shows these updates.
             * since k = ceil(c/b) when kb >= c > kb-b,
             * One-child actually has exactly the same effect as Compress.
             * thus, the code for Compress can be used fix both
             * degree and slack violations.
             * 
             * the only difference is that, in Compress,
             * the (slack) violation occurs at the topmost node, top,
             * that is replaced by the update, and in One-Child,
             * the (degree) violation occurs at a child of top.
             * thus, if the violation we found at viol is a slack violation,
             * then the leaf l that we found in our search is top.
             * otherwise, the violation we found was a degree violation,
             * so l is a child of top.
             * 
             * to facilitate the use of the same code in both cases,
             * if the violation we found was a slack violation,
             * then we take one extra step in the search,
             * so that l is a child of top.
             * this way, in each case, l is a child of top, p is top,
             * and gp is the parent of top
             * (so gp is the node whose child pointer will be changed).
             */
            if (viol.children.length() > 1) {
                // there is no degree violation at viol,
                // so we must have found a slack violation there, earlier.
                // we take one extra step in the search, so that p is top.
                ixToP = ixToL;
                ixToL = l.getChildIndex(k);
                gp = p;
                p = l;
                l = l.children.get(ixToL);
            }
            // note: p is now top
            
            // assert: gp != null (because if Compress or One-Child can be applied, then p is not the root)
            SCXRecord gpScxPtr = LLX(gp, null);
            if (gpScxPtr == FAILED || gpScxPtr == FINALIZED || gp.children.get(ixToP) != p) continue; // retry the search
            
            Node[] pChildren = new Node[p.children.length()];
            SCXRecord pScxPtr = LLX(p, pChildren);
            if (pScxPtr == FAILED || pScxPtr == FINALIZED || p.children.get(ixToL) != l) continue; // retry the search
            
            // we can only apply Compress (or One-Child) if there are no
            // weight violations at p or its children.
            // so, we first check for any weight violations,
            // and fix any that we see.
            boolean foundWeightViolation = false;
            for (int i=0;i<pChildren.length;++i) {
                if (!pChildren[i].weight) {
                    foundWeightViolation = true;
                    fixWeightViolation(pChildren[i]);
                }
            }
            if (!p.weight) {
                foundWeightViolation = true;
                fixWeightViolation(p);
            }
            // if we see any weight violations, then either we fixed one,
            // removing one of these nodes from the tree,
            // or one of the nodes has been removed from the tree by another
            // rebalancing step, so we retry the search for viol
            if (foundWeightViolation) continue;

            // assert: there are no weight violations at p or any nodes in pChildren

            int numberOfNodes;
            Node[] childrenNewP;
            K[] keysNewP;
            Node[] nodes;
            SCXRecord[] ops;

            if (pChildren[0].isLeaf()) {
                // assert: all nodes in pChildren are leaves
                // (this is because there are no weight violations any nodes in pChildren)
                
                // get the numbers of keys and pointers in the nodes of pChildren
                if (SEQUENTIAL_STAT_TRACKING) ++slackFixTotaling;
                int pGrandKeys = 0;
                for (int i=0;i<pChildren.length;++i) {
                    pGrandKeys += pChildren[i].keys.length;
                }
                int slack = pChildren.length * b - pGrandKeys;
                if (!(slack >= b + (ALLOW_ONE_EXTRA_SLACK_PER_NODE ? pChildren.length : 0))
                        && !(viol.children.length() == 1)) {
                    // there is no violation at viol
                    return false;
                }
                if (SEQUENTIAL_STAT_TRACKING) ++slackFixAttempts;

                /**
                 * if p's children are leaves, we replace these leaves
                 * with new copies that evenly share the keys/values originally
                 * contained in the children of p.
                 */
                
                ops = new SCXRecord[]{gpScxPtr, pScxPtr};
                ops[0] = gpScxPtr;
                ops[1] = pScxPtr;
                int numToRemove = 1+1+pChildren.length; // gp + p + children of p
                nodes = new Node[numToRemove];
                nodes[0] = gp;
                nodes[1] = p;
                
                // perform LLX on the children of p
                boolean failedLLX = false;
                for (int i=0;i<pChildren.length;++i) {
                    SCXRecord retval = LLX(pChildren[i], null);
                    if (retval == FAILED || retval == FINALIZED) {
                        failedLLX = true;
                        break;
                    }
                    nodes[2+i] = pChildren[i];
                }
                if (failedLLX) continue; // retry the search

                // combine keys and values of all children into big arrays
                final K[] keys = (K[]) new Comparable[pGrandKeys];
                final V[] values = (V[]) new Object[pGrandKeys];
                pGrandKeys = 0;
                for (int i=0;i<pChildren.length;++i) {
                    System.arraycopy(pChildren[i].keys, 0, keys, pGrandKeys, pChildren[i].keys.length);
                    System.arraycopy(pChildren[i].values, 0, values, pGrandKeys, pChildren[i].values.length);
                    pGrandKeys += pChildren[i].keys.length;
                }

                // determine how to divide keys&values into leaves as evenly as possible.
                // specifically, we divide them into nodesWithCeil + nodesWithFloor leaves,
                // containing keysPerNodeCeil and keysPerNodeFloor keys, respectively.
                if (ALLOW_ONE_EXTRA_SLACK_PER_NODE) {
                    numberOfNodes = (pGrandKeys + (b-2)) / (b-1); // how many leaves?
                } else {
                    numberOfNodes = (pGrandKeys + (b-1)) / b;
                }
                int keysPerNodeCeil = (pGrandKeys + (numberOfNodes-1)) / numberOfNodes;
                int keysPerNodeFloor = pGrandKeys / numberOfNodes;
                int nodesWithCeil = pGrandKeys % numberOfNodes;
                int nodesWithFloor = numberOfNodes - nodesWithCeil;

                // divide keys&values into leaves of degree keysPerNodeCeil
                childrenNewP = (Node[]) new Node[numberOfNodes];
                for (int i=0;i<nodesWithCeil;++i) {
                    final K[] keysNode = (K[]) new Comparable[keysPerNodeCeil];
                    final V[] valuesNode = (V[]) new Object[keysPerNodeCeil];
                    System.arraycopy(keys, keysPerNodeCeil*i, keysNode, 0, keysPerNodeCeil);
                    System.arraycopy(values, keysPerNodeCeil*i, valuesNode, 0, keysPerNodeCeil);
                    assert(keysNode.length > 0);
                    // note: the search key keysNode[0] exists because,
                    // if we enter this loop, then there are at least two new children.
                    // this means each child contains at least floor(b/2) > 0 keys
                    // (or floor(b/2)-2 > 0 when ALLOW_ONE_EXTRA_SLACK_PER_NODE is true).
                    childrenNewP[i] = new Node(keysNode, valuesNode, null, true, keysNode[0]);
                }

                // divide remaining keys&values into leaves of degree keysPerNodeFloor
                for (int i=0;i<nodesWithFloor;++i) {
                    final K[] keysNode = (K[]) new Comparable[keysPerNodeFloor];
                    final V[] valuesNode = (V[]) new Object[keysPerNodeFloor];
                    System.arraycopy(keys, keysPerNodeCeil*nodesWithCeil+keysPerNodeFloor*i, keysNode, 0, keysPerNodeFloor);
                    System.arraycopy(values, keysPerNodeCeil*nodesWithCeil+keysPerNodeFloor*i, valuesNode, 0, keysPerNodeFloor);
                    assert(keysNode.length > 0);
                    // note the following search key assignment is rather odd.
                    // let me explain why it makes sense.
                    // if there are two or more new children,
                    // then each contains contains at least floor(b/2) > 0 keys
                    // (or floor(b/2)-2 > 0 when ALLOW_ONE_EXTRA_SLACK_PER_NODE is true),
                    // so child will contain at least 1 key, and keysNode[0] is the first.
                    // if there is only one new child, then the new child will still be
                    // reachable by searching for the same key as the old first child of p.
                    // (we use pChildren[0].searchKey instead of keys[0] because keys
                    //  might in fact contain ZERO keys!)
                    childrenNewP[i+nodesWithCeil] = new Node(keysNode, valuesNode, null, true,
                            (numberOfNodes == 1 ? pChildren[0].searchKey : keysNode[0]));
                }

                // build keys array for new parent
                keysNewP = (K[]) new Comparable[numberOfNodes-1];
                for (int i=1;i<numberOfNodes;++i) {
                    keysNewP[i-1] = (K) childrenNewP[i].keys[0];
                }

            } else {
                // assert: all nodes in pChildren are internal
                // (since there are no weight violations at the nodes in pChildren)

                // get the numbers of keys and pointers in the nodes of pChildren
                if (SEQUENTIAL_STAT_TRACKING) ++slackFixTotaling;
                int pGrandChildren = 0;
                for (int i=0;i<pChildren.length;++i) {
                    pGrandChildren += pChildren[i].children.length();
                }
                int slack = pChildren.length * b - pGrandChildren;
                if (!(slack >= b + (ALLOW_ONE_EXTRA_SLACK_PER_NODE ? pChildren.length : 0))
                        && !(viol.children.length() == 1)) {
                    // there is no violation at viol
                    return false;
                }
                if (SEQUENTIAL_STAT_TRACKING) ++slackFixAttempts;

                /**
                 * if p's children are internal nodes, we replace these nodes
                 * with new copies that evenly share the keys/pointers originally
                 * contained in the children of p.
                 */
                
                final int numToFreeze = 1+1+pChildren.length; // gp + p + children of p
                final int numToRemove = numToFreeze;
                nodes = new Node[numToFreeze];
                nodes[0] = gp;
                nodes[1] = p;
                ops = new SCXRecord[numToRemove];
                ops[0] = gpScxPtr;
                ops[1] = pScxPtr;
                
                // perform LLX on the children of p
                boolean failedLLX = false;
                for (int i=0;i<pChildren.length;++i) {
                    if (!LLX(pChildren[i], null, 2+i, ops, nodes)) {
                        failedLLX = true;
                        break;
                    }
                }
                if (failedLLX) continue; // retry the search
                
                // combine keys and children of all children of p into big arrays
                final K[] keys = (K[]) new Comparable[pGrandChildren-1];
                final Node[] children = (Node[]) new Node[pGrandChildren];
                pGrandChildren = 0;
                for (int i=0;i<p.children.length();++i) {
                    System.arraycopy(pChildren[i].keys, 0, keys, pGrandChildren, pChildren[i].keys.length);
                    arraycopy(pChildren[i].children, 0, children, pGrandChildren, pChildren[i].children.length());
                    pGrandChildren += pChildren[i].children.length();
                    // since we have one less key than children, we fill the hole
                    // with the key of p to the right of this child pointer.
                    // (we can't do this for the last child of p, but we don't
                    //  need to, since that last hole doesn't need to be filled.)
                    if (i < p.keys.length) keys[pGrandChildren-1] = (K) p.keys[i];
                }

                // determine how to divide keys&pointers into leaves as evenly as possible.
                // specifically, we divide them into nodesWithCeil + nodesWithFloor leaves,
                // containing childrenPerNodeCeil and childrenPerNodeFloor pointers, respectively.
                if (ALLOW_ONE_EXTRA_SLACK_PER_NODE) {
                    numberOfNodes = (pGrandChildren + (b-2)) / (b-1);
                } else {
                    numberOfNodes = (pGrandChildren + (b-1)) / b;
                }
                int childrenPerNodeCeil = (pGrandChildren + (numberOfNodes-1)) / numberOfNodes;
                int childrenPerNodeFloor = pGrandChildren / numberOfNodes;
                int nodesWithCeil = pGrandChildren % numberOfNodes;
                int nodesWithFloor = numberOfNodes - nodesWithCeil;

                // divide keys&pointers into internal nodes of degree childrenPerNodeCeil
                childrenNewP = (Node[]) new Node[numberOfNodes];
                for (int i=0;i<nodesWithCeil;++i) {
                    final K[] keysNode = (K[]) new Comparable[childrenPerNodeCeil-1];
                    final Node[] childrenNode = (Node[]) new Node[childrenPerNodeCeil];
                    System.arraycopy(keys, childrenPerNodeCeil*i, keysNode, 0, childrenPerNodeCeil-1);
                    System.arraycopy(children, childrenPerNodeCeil*i, childrenNode, 0, childrenPerNodeCeil);
                    assert(keysNode.length > 0);
                    // note: the search key keysNode[0] exists because,
                    // if we enter this loop, then there are at least two new children.
                    // this means each child contains at least floor(b/2) > 0 keys
                    // (or floor(b/2)-2 > 0 when ALLOW_ONE_EXTRA_SLACK_PER_NODE is true).
                    childrenNewP[i] = new Node(keysNode, null, new AtomicReferenceArray<>(childrenNode), true, keysNode[0]);
                }

                // divide remaining keys&pointers into internal nodes of degree childrenPerNodeFloor
                for (int i=0;i<nodesWithFloor;++i) {
                    final K[] keysNode = (K[]) new Comparable[childrenPerNodeFloor-1];
                    final Node[] childrenNode = (Node[]) new Node[childrenPerNodeFloor];
                    System.arraycopy(keys, childrenPerNodeCeil*nodesWithCeil+childrenPerNodeFloor*i, keysNode, 0, childrenPerNodeFloor-1);
                    System.arraycopy(children, childrenPerNodeCeil*nodesWithCeil+childrenPerNodeFloor*i, childrenNode, 0, childrenPerNodeFloor);
                    assert(keysNode.length > 0);
                    // note the following search key assignment is rather odd.
                    // let me explain why it makes sense.
                    // if there are two or more new children,
                    // then each contains contains at least floor(b/2) > 0 keys
                    // (or floor(b/2)-2 > 0 when ALLOW_ONE_EXTRA_SLACK_PER_NODE is true),
                    // so child will contain at least 1 key, and keysNode[0] is the first.
                    // if there is only one new child, then the new child will still be
                    // reachable by searching for the same key as the old first child of p.
                    // (we use pChildren[0].searchKey instead of keys[0] because keys
                    //  might in fact contain ZERO keys!)
                    childrenNewP[i+nodesWithCeil] = new Node(keysNode, null, new AtomicReferenceArray<>(childrenNode), true,
                            (numberOfNodes == 1 ? pChildren[0].searchKey : keysNode[0]));
                }

                // build keys array for new parent
                keysNewP = (K[]) new Comparable[numberOfNodes-1];
                for (int i=0;i<nodesWithCeil;++i) {
                    keysNewP[i] = keys[childrenPerNodeCeil*i + childrenPerNodeCeil-1];
                }
                for (int i=0;i<nodesWithFloor-1;++i) { // this is nodesWithFloor - 1 because we want to go up to numberOfNodes - 1, not numberOfNodes.
                    keysNewP[i+nodesWithCeil] = keys[childrenPerNodeCeil*nodesWithCeil + childrenPerNodeFloor*i + childrenPerNodeFloor-1];
                }
            }

            if (SEQUENTIAL_STAT_TRACKING) ++slackFixSCX;
            
            // now, we atomically replace p and its children with the new nodes.
            // if appropriate, we perform Root-Replace at the same time.
            if (gp == root && childrenNewP.length == 1) {
                /**
                 * Compress/One-Child AND Root-Replace.
                 */
                SCXRecord scxPtr = new SCXRecord(nodes, ops, childrenNewP[0], ixToP);
                if (helpSCX(scxPtr, 0)) {
                    if (DRAW_TREES_FOR_EACH_OPERATION) Tests.renderTree((KSTNode<Integer>) getRoot(), "after-op" + (++operationCount) + "-compressroot-"+viol.searchKey, true);
                    if (SEQUENTIAL_STAT_TRACKING) ++slackFixes;
                    
                    //    compress [check: slack@children(n), slack@p, degree@n]
                    //        no weight at u
                    //        no degree at u
                    //        slack at u -> eliminated
                    //        no weight at any child of u
                    //        degree at a child of u -> eliminated
                    //        slack at a child of u -> eliminated or slack at a child of n
                    //        [maybe create slack at any or all children of n]
                    //        [maybe create slack at p]
                    //        [maybe create degree at n]

                    //    root-replace [check: degree@n, slack@n]
                    //        no weight at root
                    //        degree at root -> eliminated
                    //        slack at root -> eliminated
                    //        weight at child of root -> eliminated
                    //        degree at child of root -> degree at n
                    //        slack at child of root -> slack at n
                    
                    // in this case, the compress creates a node n with exactly
                    // one child. this child may have a slack violation, and
                    // n may have a degree violation. additionally, p may have
                    // a slack violation.
                    // however, after we also perform root-replace, n is removed
                    // altogether, so there are no violations at n.
                    // note that the n in the root-replace comment above refers
                    // to the single child of the node n referred to by the
                    // compress comment.
                    // thus, the only possible violations after the root-replace
                    // are a slack violation at the child, a degree violation
                    // at the child, and a slack violation at p.
                    // we check for (and attempt to fix) each of these.
                    fixDegreeOrSlackViolation(childrenNewP[0]);
                    // note: it is impossible for there to be a weight violation at childrenNewP or p, since these nodes must have weight=true for the compress/one-child+root-replace operation to be applicable, and we consequently CREATE childrenNewP[0] and p with weight=true above
                    return true;
                }
            } else {
                /**
                 * Compress/One-Child.
                 */
                final Node newP = new Node(keysNewP, null, new AtomicReferenceArray<>(childrenNewP), true, p.searchKey);
                SCXRecord scxPtr = new SCXRecord(nodes, ops, newP, ixToP);
                if (helpSCX(scxPtr, 0)) {
                    if (DRAW_TREES_FOR_EACH_OPERATION) Tests.renderTree((KSTNode<Integer>) getRoot(), "after-op" + (++operationCount) + "-compress-"+viol.searchKey, true);
                    if (SEQUENTIAL_STAT_TRACKING) ++slackFixes;
                    
                    //    compress [check: slack@children(n), slack@p, degree@n]
                    //        no weight at u
                    //        no degree at u
                    //        slack at u -> eliminated
                    //        no weight at any child of u
                    //        degree at a child of u -> eliminated
                    //        slack at a child of u -> eliminated or slack at a child of n
                    //        [maybe create slack at any or all children of n]
                    //        [maybe create slack at p]
                    //        [maybe create degree at n]
                    //    
                    //    one-child [check: slack@children(n)]
                    //        no weight at u
                    //        degree at u -> eliminated
                    //        slack at u -> eliminated or slack at a child of n
                    //        no weight any sibling of u or pi(u)
                    //        no degree at any sibling of u or pi(u)
                    //        slack at a sibling of u -> eliminated or slack at a child of n
                    //        no slack at pi(u)
                    //        [maybe create slack at any or all children of n]
                    
                    // note that, in the above comment, slack@p actually refers to
                    // a possible slack violation at the parent of the topmost node
                    // that is replaced by the update. here, the topmost node
                    // replaced by the update is p, so we must actually check
                    // for a slack violation at gp.
                    
                    for (int i=0;i<childrenNewP.length;++i) {
                        fixDegreeOrSlackViolation(childrenNewP[i]);
                    }
                    fixDegreeOrSlackViolation(newP);
                    fixDegreeOrSlackViolation(gp);
                }
            }
        }
    }
    
    private void arraycopy(AtomicReferenceArray<Node> src, int srcStart, Node[] dest, int destStart, int len) {
        for (int i=0;i<len;++i) {
            dest[destStart+i] = src.get(srcStart+i);
        }
    }
    
    private boolean LLX(final Node r, final Node[] snapshot, final int i, final SCXRecord[] ops, final Node[] nodes) {
        SCXRecord result = LLX(r, snapshot);
        if (result == FAILED || result == FINALIZED) return false;
        ops[i] = result;
        nodes[i] = r;
        return true;
    }

    private SCXRecord LLX(final Node r, final Node[] snapshot) {
        final boolean marked = r.marked;
        final SCXRecord rinfo = r.scxPtr;
        final int state = rinfo.state;
        if (state == SCXRecord.STATE_ABORTED || (state == SCXRecord.STATE_COMMITTED && !r.marked)) {
            // read snapshot fields
            if (snapshot != null) {
                arraycopy(r.children, 0, snapshot, 0, r.children.length());
            }
            if (r.scxPtr == rinfo) return rinfo; // we have a snapshot
        }
        if (marked && (rinfo.state == SCXRecord.STATE_COMMITTED || (rinfo.state == SCXRecord.STATE_INPROGRESS && helpSCX(rinfo, 1)))) {
            return FINALIZED;
        } else {
            if (r.scxPtr.state == SCXRecord.STATE_INPROGRESS) helpSCX(r.scxPtr, 1);
            return FAILED;
        }
    }
        
    // this function is essentially an SCX without the creation of V, R, fld, new
    // (which are stored in an operation object).
    // the creation of the operation object is simply inlined in other methods.
    private boolean helpSCX(final SCXRecord scxPtr, int i) {
        // get local references to some fields of scxPtr, in case we later null out fields of scxPtr (to help the garbage collector)
        final int index = scxPtr.index;
        final Node[] nodes = scxPtr.nodes;
        final SCXRecord[] ops = scxPtr.ops;
        final Node subtree = scxPtr.subtree;
        // if we see aborted or committed, no point in helping (already done).
        // further, if committed, variables may have been nulled out to help the garbage collector.
        // so, we return.
        if (scxPtr.state != SCXRecord.STATE_INPROGRESS) return true;
        
        // freeze sub-tree
        for (; i<ops.length; ++i) {
            if (!updateScxPtr.compareAndSet(nodes[i], ops[i], scxPtr) && nodes[i].scxPtr != scxPtr) { // if work was not done
                if (scxPtr.allFrozen) {
                    return true;
                } else {
                    scxPtr.state = SCXRecord.STATE_ABORTED;
                    // help the garbage collector (must be AFTER we set state committed or aborted)
                    scxPtr.nodes = null;
                    scxPtr.ops = null;
                    scxPtr.subtree = null;
                    return false;
                }
            }
        }
        scxPtr.allFrozen = true;
        for (i=1; i<ops.length; ++i) nodes[i].marked = true; // finalize all but first node
        
        // CAS in the new sub-tree (child-cas)
        nodes[0].children.compareAndSet(index, nodes[1], subtree);     // splice in new sub-tree
        scxPtr.state = SCXRecord.STATE_COMMITTED;
        
        // help the garbage collector (must be AFTER we set state committed or aborted)
        scxPtr.nodes = null;
        scxPtr.ops = null;
        scxPtr.subtree = null;
        return true;
    }
    
    public static class Node {
        public final Object[] keys;
        public final Object[] values;
        public final AtomicReferenceArray<Node> children;
        public final boolean weight;
        public final Object searchKey;
        public volatile SCXRecord scxPtr;
        public volatile boolean marked;
        
        public Node(final Object[] keys, final Object[] values, final AtomicReferenceArray<Node> children, final boolean weight, final Object searchKey) {
            this.keys = keys;
            this.values = values;
            this.children = children;
            this.weight = weight;
            this.scxPtr = DUMMY;
            this.searchKey = searchKey;
        }
        
        public boolean isLeaf() {
            return children == null;
        }
        
        public boolean containsKey(final Comparable key) {
            return containsKey(getKeyIndex(key));
        }
        public boolean containsKey(final int keyIndex) {
            return (keyIndex >= 0);
        }
        
        /**
         * if this returns a negative value, add one and then negate it to
         * learn where key should appear in the array.
         */
        public int getKeyIndex(final Comparable key) {
            if (keys == null || keys.length == 0) return -1;
            return Arrays.binarySearch(keys, key, null);
        }
        
        public int getChildIndex(final Node child) {
            if (child == null) return 0;
            if (child.keys == null) return 0;
            return getChildIndex((Comparable) child.keys[0]);
        }
        public int getChildIndex(final Comparable key) {
            return getChildIndex(getKeyIndex(key), key);
        }
        public int getChildIndex(int keyIndex, final Comparable key) {
            if (keyIndex < 0) {         // key not in node
                return -(keyIndex + 1); // return position of first key greater than key (i.e., the position key would be at)
            } else {                    // key in node
                return keyIndex + 1;    // return 1+position key is at
            }
        }
        
        public String getKeysString() {
            StringBuilder sb = new StringBuilder();
            for (int i=0;i<keys.length;++i) {
                sb.append(keys[i]);
                if (i+1 < keys.length) sb.append(",");
            }
            return sb.toString();
        }
        
        public String toString() {
            StringBuilder sb = new StringBuilder();
            sb.append("[@");
            sb.append(Integer.toHexString(hashCode()));
            sb.append(" weight=");
            sb.append(weight);
            sb.append(" values=");
            sb.append(values);
            sb.append(" children=");
            sb.append(children);
            sb.append(" keys.length=");
            sb.append(keys.length);
            sb.append(" keys={");
            sb.append(getKeysString());
            sb.append("}");
            sb.append("]");
            return sb.toString();
        }
    }
    
    public static final class SCXRecord {
        final static int STATE_INPROGRESS = 0;
        final static int STATE_ABORTED = 1;
        final static int STATE_COMMITTED = 2;
        
        volatile int index; // index in nodes[0].children of the pointer to change
        volatile Node subtree;
        volatile Node[] nodes;
        volatile SCXRecord[] ops;
        volatile int state;
        volatile boolean allFrozen;

        public SCXRecord() {            // create an dummy operation [[ we have null fields rather than smaller records simply to avoid the overhead of inheritance ]]
            state = STATE_ABORTED; // cheap trick to piggy-back on a pre-existing check for frozen nodes
        }

        public SCXRecord(final Node[] nodes, final SCXRecord[] ops, final Node subtree, final int index) {
            this.nodes = nodes;
            this.ops = ops;
            this.subtree = subtree;
            this.index = index;
        }
    }

    
    
    
    
    
    
    /*******************************************************************
     * Utility functions for integration with the test harness
     *******************************************************************/
    
    public final int getNumberOfNodes() {
        return getNumberOfLeaves() + getNumberOfInternals();
    }

    public final int getNumberOfLeaves() {
        return getNumberOfLeaves(root.children.get(0));
    }
    private int getNumberOfLeaves(Node node) {
        if (node == null) return 0;
        if (node.isLeaf()) return 1;
        int result = 0;
        for (int i=0;i<node.children.length();++i) {
            result += getNumberOfLeaves(node.children.get(i));
        }
        return result;
    }

    public final int getNumberOfInternals() {
        return getNumberOfInternals(root.children.get(0));
    }
    private int getNumberOfInternals(Node node) {
        if (node == null) return 0;
        if (node.isLeaf()) return 0;
        int result = 1;
        for (int i=0;i<node.children.length();++i) {
            result += getNumberOfInternals(node.children.get(i));
        }
        return result;
    }
    
    public final double getAverageKeyDepth() {
        long sz = sequentialSize();
        return (sz == 0) ? 0 : getSumOfKeyDepths() / sz;
    }
    public final int getSumOfKeyDepths() {
        return getSumOfKeyDepths(root.children.get(0), 0);
    }
    private int getSumOfKeyDepths(Node node, int depth) {
        if (node == null) return 0;
        if (node.isLeaf()) return depth * node.keys.length;
        int result = 0;
        for (int i=0;i<node.children.length();i++) {
            result += getSumOfKeyDepths(node.children.get(i), 1+depth);
        }
        return result;
    }
    
    public final int getHeight() {
        return getHeight(root.children.get(0), 0);
    }
    private int getHeight(Node node, int depth) {
        if (node == null) return 0;
        if (node.isLeaf()) return 0;
        int result = 0;
        for (int i=0;i<node.children.length();i++) {
            int retval = getHeight(node.children.get(i), 1+depth);
            if (retval > result) result = retval;
        }
        return result+1;
    }
    
    public double getAverageDegree() {
        return getTotalDegree(root) / (double) getNodeCount(root);
    }
    public double getSpacePerKey() {
        return getNodeCount(root)*2*b / (double) getKeyCount(root);
    }
    private int getKeyCount(Node root) {
        if (root == null) return 0;
        if (root.isLeaf()) return root.keys.length;
        int sum = 0;
        for (int i=0;i<root.children.length();++i) {
            sum += getKeyCount(root.children.get(i));
        }
        return sum;
    }
    private int getTotalDegree(Node root) {
        if (root == null) return 0;
        int sum = root.keys.length;
        if (root.isLeaf()) return sum;
        for (int i=0;i<root.children.length();++i) {
            sum += getTotalDegree(root.children.get(i));
        }
        return 1+sum; // one more children than keys
    }
    private int getNodeCount(Node root) {
        if (root == null) return 0;
        if (root.isLeaf()) return 1;
        int sum = 1;
        for (int i=0;i<root.children.length();++i) {
            sum += getNodeCount(root.children.get(i));
        }
        return sum;
    }
    
    public long getSumOfKeys() {
        return getSumOfKeys(root);
    }
    private long getSumOfKeys(Node node) {
        long sum = 0;
        if (node.isLeaf()) {
            for (int i=0;i<node.keys.length;++i) {
                sum += (int) (Integer) node.keys[i];
            }
        } else {
            for (int i=0;i<node.children.length();++i) {
                sum += getSumOfKeys(node.children.get(i));
            }
        }
        return sum;
    }
    
    /**
     * Functions for verifying that the data structure is a B-slack tree
     */
    
    private boolean satisfiesP1() {
        return satisfiesP1(root.children.get(0), getHeight(), 0);
    }
    private boolean satisfiesP1(Node node, int height, int depth) {
        if (node.isLeaf()) return (height == depth);
        for (int i=0;i<node.children.length();++i) {
            if (!satisfiesP1(node.children.get(i), height, depth+1)) return false;
        }
        return true;
    }
    
    private boolean satisfiesP2() {
        return satisfiesP2(root.children.get(0));
    }
    private boolean satisfiesP2(Node node) {
        if (node.isLeaf()) return true;
        if (node.children.length() < 2) return false;
        if (node.keys.length + 1 != node.children.length()) return false;
        for (int i=0;i<node.children.length();++i) {
            if (!satisfiesP2(node.children.get(i))) return false;
        }
        return true;
    }
    
    private boolean leavesHaveValueForEachKey() {
        return leavesHaveValueForEachKey(root.children.get(0));
    }
    private boolean leavesHaveValueForEachKey(Node node) {
        if (!node.isLeaf()) return true;
        if (node.keys.length != node.values.length) return false;
        for (int i=0;i<node.children.length();++i) {
            if (!leavesHaveValueForEachKey(node.children.get(i))) return false;
        }
        return true;
    }
    
    private boolean noWeightViolations() {
        return noWeightViolations(root.children.get(0));
    }
    private boolean noWeightViolations(Node node) {
        if (!node.weight) return false;
        if (!node.isLeaf()) {
            for (int i=0;i<node.children.length();++i) {
                if (!noWeightViolations(node.children.get(i))) return false;
            }
        }
        return true;
    }
    
    private boolean childrenAreAllLeavesOrInternal() {
        return childrenAreAllLeavesOrInternal(root.children.get(0));
    }
    private boolean childrenAreAllLeavesOrInternal(Node node) {
        if (node.isLeaf()) return true;
        boolean leafChild = false;
        for (int i=0;i<node.children.length();++i) {
            if (node.children.get(i).isLeaf()) leafChild = true;
            else if (leafChild) return false;
        }
        return true;
    }
    
    private boolean satisfiesP4() {
        return satisfiesP4(root.children.get(0));
    }
    private boolean satisfiesP4(Node node) {
        // note: this function assumes that childrenAreAllLeavesOrInternal() = true
        if (node.isLeaf()) return true;
        int totalDegreeOfChildren = 0;
        for (int i=0;i<node.children.length();++i) {
            Node c = node.children.get(i);
            if (!satisfiesP4(node.children.get(i))) return false;
            totalDegreeOfChildren += (c.isLeaf() ? c.keys.length : c.children.length());
        }
        int slack = node.children.length() * b - totalDegreeOfChildren;
        if (slack >= b + (ALLOW_ONE_EXTRA_SLACK_PER_NODE ? node.children.length() : 0)) {
            System.err.println("ERROR: slack=" + slack + " at node " + node);
            printTree();
            return false;
        }
        return true;
    }
    
    private boolean isBSlackTree() {
        if (!satisfiesP1()) throw new RuntimeException("satisfiesP1() == false");
        if (!satisfiesP2()) throw new RuntimeException("satisfiesP2() == false");
        if (!leavesHaveValueForEachKey()) throw new RuntimeException("leavesHaveValueForEachKey() == false");
        if (!noWeightViolations()) throw new RuntimeException("noWeightViolations() == false");
        if (!childrenAreAllLeavesOrInternal()) throw new RuntimeException("childrenAreAllLeavesOrInternal() == false");
        if (!satisfiesP4()) throw new RuntimeException("satisfiesP4() == false");
        return true;
    }
    
}