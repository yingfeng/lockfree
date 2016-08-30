package algorithms.published;

/**
 *  An implementation of a non-blocking k-ary search tree with k=4.
 *  Copyright (C) 2011  Trevor Brown, Joanna Helga
 *  Contact Trevor Brown (tabrown@cs.toronto.edu) with any questions or comments.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

import java.util.*;
import java.util.concurrent.atomic.*;

public class LockFree4ST<E extends Comparable<? super E>, V> {

    /**
     *
     * GLOBALS
     *
     */

    private static final AtomicReferenceFieldUpdater<Node, Node> c0Updater =
        AtomicReferenceFieldUpdater.newUpdater(Node.class, Node.class, "c0");
    private static final AtomicReferenceFieldUpdater<Node, Node> c1Updater =
        AtomicReferenceFieldUpdater.newUpdater(Node.class, Node.class, "c1");
    private static final AtomicReferenceFieldUpdater<Node, Node> c2Updater =
        AtomicReferenceFieldUpdater.newUpdater(Node.class, Node.class, "c2");
    private static final AtomicReferenceFieldUpdater<Node, Node> c3Updater =
        AtomicReferenceFieldUpdater.newUpdater(Node.class, Node.class, "c3");

    private static final AtomicReferenceFieldUpdater<Node, Info> infoUpdater =
        AtomicReferenceFieldUpdater.newUpdater(Node.class, Info.class, "info");
    private final Node<E,V> root;



    /**
     *
     * CONSTRUCTORS
     *
     */

    public LockFree4ST() {
        this.root = new Node<E,V>(true);
    }

    private LockFree4ST(Node root) {
        this.root = root;
    }



    /**
     *
     * PUBLIC FUNCTIONS
     *
     */

    /**
     * Determines whether a key is present in the tree.
     * @return true if the key is present in the tree, and false otherwise
     * @throws NullPointerException in the event that key is null
     */
    public final boolean containsKey(final E key) {
        if (key == null) throw new NullPointerException();
        Node<E,V> l = root.c0;
        while (l.c0 != null) l = child(key, l);  /* while l is internal */
        return l.hasKey(key);
    }

    /**
     * Retrieves the value associated with key from the tree.
     * @return the value mapped to the key, or null in the event that
     *         (1) the key is not present in the tree, or
     *         (2) the value null is stored with the key
     * @throws NullPointerException in the event that key is null
     */
    public final V get(final E key) {
        if (key == null) throw new NullPointerException();
        Node<E,V> l = root.c0;
        while (l.c0 != null) l = child(key, l);  /* while l is internal */
        return l.getValue(key);
    }

    /**
     * Adds a key-value pair to the tree if the key does not already exist.
     * @return the previous value mapped to the key, or null in the event that
     *         (1) the key was not previously in the tree and the new
     *             value was successfully assigned, or
     *         (2) the key existed in the tree,
     *             and the value stored with it was null.
     * @throws NullPointerException in the event that key is null
     */
    public final V putIfAbsent(final E key, final V value) {
        if (key == null) throw new NullPointerException();
        Node<E,V> p, l, newchild;
        Info<E,V> pinfo;
        int pindex; // index of the child of p that points to l

        while (true) {
            // search
            p = root;
            pinfo = p.info;
            l = p.c0;
            while (l.c0 != null) {
                p = l;
                l = child(key, l);
            }

            // - read gpinfo once instead of every iteration of the previous loop
            // then re-read and verify the child pointer from gp to p
            // (so it is as if gp.info were read first)
            // and also store the index of the child pointer of gp that points to p
            pinfo = p.info;
            Node<E,V> currentL;
            if (less(key, p.k0)) { currentL = p.c0; pindex = 0; }
            else if (less(key, p.k1)) { currentL = p.c1; pindex = 1; }
            else if (less(key, p.k2)) { currentL = p.c2; pindex = 2; }
            else { currentL = p.c3; pindex = 3; }

            if (l != currentL) continue;

            if (l.hasKey(key)) return l.getValue(key);
            else if (pinfo != null && pinfo.getClass() != Clean.class) help(pinfo);
            else {
                // SPROUTING INSERTION
                if (l.kcount == 3) { // l is full of keys
                    // create internal node with 4 children sorted by key
                    newchild = new Node<E,V>(key, value, l);

                // SIMPLE INSERTION
                } else {
                    // create leaf node with sorted keys
                    // (at least one key of l is null and, by structural invariant,
                    // nulls appear at the end, so the last key is null)
                    newchild = new Node<E,V>(key, value, l, false);
                }

                // flag and perform the insertion
                final IInfo<E,V> newPInfo = new IInfo<E,V>(l, p, newchild, pindex);
                if (infoUpdater.compareAndSet(p, pinfo, newPInfo)) {	    // [[ iflag CAS ]]
                    helpInsert(newPInfo);
                    return null;
                } else {
                    // help current operation first
                    help(p.info);
                }
            }
        }
    }

    /**
     * Adds a key-value pair to the tree, overwriting any pre-existing mapping.
     * @return the value that was previously mapped to the key, or null if the
     *         key was not in the tree (or if the value stored with it was null)
     * @throws NullPointerException in the event that key is null
     */
    public final V put(final E key, final V value) {
        if (key == null) throw new NullPointerException();
        Node<E, V> p, l, newchild;
        Info<E, V> pinfo;
        int pindex; // index of the child of p that points to l

        while (true) {
            // search
            p = root;
            pinfo = p.info;
            l = p.c0;
            while (l.c0 != null) {
                p = l;
                l = child(key, l);
            }

            // - read gpinfo once instead of every iteration of the previous loop
            // then re-read and verify the child pointer from gp to p
            // (so it is as if gp.info were read first)
            // and also store the index of the child pointer of gp that points to p
            pinfo = p.info;
            Node<E,V> currentL;
            if (less(key, p.k0)) { currentL = p.c0; pindex = 0; }
            else if (less(key, p.k1)) { currentL = p.c1; pindex = 1; }
            else if (less(key, p.k2)) { currentL = p.c2; pindex = 2; }
            else { currentL = p.c3; pindex = 3; }

            if (l != currentL) continue;

            if (pinfo != null && pinfo.getClass() != Clean.class) {
                help(pinfo);
            } else if (l.hasKey(key)) {

                // REPLACE INSERTION
                newchild = new Node<E, V>(key, value, l, true); // true means key is already in node l

                // flag and perform the insertion
                final IInfo<E, V> newPInfo = new IInfo<E, V>(l, p, newchild, pindex);
                if (infoUpdater.compareAndSet(p, pinfo, newPInfo)) {	    // [[ iflag CAS ]]
                    helpInsert(newPInfo);
                    return l.getValue(key);
                } else {
                    // help current operation first
                    help(p.info);
                }
            } else {
                // SPROUTING INSERTION
                if (l.kcount == 3) { // l is full of keys
                    // create internal node with 4 children sorted by key
                    newchild = new Node<E, V>(key, value, l);

                // SIMPLE INSERTION
                } else {
                    // create leaf node with sorted keys
                    // (at least one key of l is null and, by structural invariant,
                    // nulls appear at the end, so the last key is null)
                    newchild = new Node<E, V>(key, value, l, false); // false means key is not in l
                }

                // flag and perform the insertion
                final IInfo<E, V> newPInfo = new IInfo<E, V>(l, p, newchild, pindex);
                if (infoUpdater.compareAndSet(p, pinfo, newPInfo)) {	    // [[ iflag CAS ]]
                    helpInsert(newPInfo);
                    return null;
                } else {
                    // help current operation first
                    help(p.info);
                }
            }
        }
    }

    /**
     * Remove a key from the tree.
     * @return the value that was removed from the tree, or null if the key was
     *         not in the tree (or if the value stored with it was null)
     * @throws NullPointerException in the event that key is null
     */
    public final V remove(final E key) {
        if (key == null) throw new NullPointerException();
        Node<E,V> gp, p, l, newchild;
        Info<E,V> gpinfo, pinfo;
        int pindex;  // index of the child of p that points to l
        int gpindex; // index of the child of gp that points to p

        while (true) {
            // search
            gp = null;
            gpinfo = null;
            p = root;
            pinfo = p.info;
            l = p.c0;
            while (l.c0 != null) {
                gp = p;
                p = l;
                l = child(key, l);
            }

            // - read gpinfo once instead of every iteration of the previous loop
            // then re-read and verify the child pointer from gp to p
            // (so it is as if gp.info were read first)
            // and also store the index of the child pointer of gp that points to p
            gpinfo = gp.info;
            Node<E,V> currentP;
            if (less(key, gp.k0)) { currentP = gp.c0; gpindex = 0; }
            else if (less(key, gp.k1)) { currentP = gp.c1; gpindex = 1; }
            else if (less(key, gp.k2)) { currentP = gp.c2; gpindex = 2; }
            else { currentP = gp.c3; gpindex = 3; }

            if (p != currentP) continue;

            // - then do the same for pinfo and the child pointer from p to l
            pinfo = p.info;
            Node<E,V> currentL;
            if (less(key, p.k0)) { currentL = p.c0; pindex = 0; }
            else if (less(key, p.k1)) { currentL = p.c1; pindex = 1; }
            else if (less(key, p.k2)) { currentL = p.c2; pindex = 2; }
            else { currentL = p.c3; pindex = 3; }

            if (l != currentL) continue;

            // if the key is not in the tree, return null
            if (!l.hasKey(key))
                return null;
            else if (gpinfo != null && gpinfo.getClass() != Clean.class)
                help(gpinfo);
            else if (pinfo != null && pinfo.getClass() != Clean.class)
                help(pinfo);
            else {
                // count number of non-empty children of p
                final int ccount = (p.c0.kcount > 0 ? 1 : 0) +
                                   (p.c1.kcount > 0 ? 1 : 0) +
                                   (p.c2.kcount > 0 ? 1 : 0) +
                                   (p.c3.kcount > 0 ? 1 : 0);


                // PRUNING DELETION
                if (ccount == 2 && l.kcount == 1) {
                    final DInfo<E,V> newGPInfo = new DInfo<E,V>(l, p, gp, pinfo, gpindex);
                    if (infoUpdater.compareAndSet(gp, gpinfo, newGPInfo)) { // [[ dflag CAS ]]
                        if (helpDelete(newGPInfo)) return l.getValue(key);
                    } else {
                        help(gp.info);
                    }

		// SIMPLE DELETION
                } else {
                    // create leaf with sorted keys
                    newchild = new Node<E,V>(key, l, l.kcount - 1);

                    // flag and perform the key deletion (like insertion)
                    final IInfo<E,V> newPInfo = new IInfo<E,V>(l, p, newchild, pindex);
                    if (infoUpdater.compareAndSet(p, pinfo, newPInfo)) {	// [[ kdflag CAS ]]
                        helpInsert(newPInfo);
                        return l.getValue(key);
                    } else {
                        help(p.info);
                    }
                }
            }
        }
    }

    /**
     * Determines the size of the tree
     * @return the size of the tree, or -1 if the tree was concurrently modified
     */
    public final int size() {
        Node newroot = getSnapshot();
        if (newroot == null) return -1;
        return sequentialSize(newroot);
    }

    /**
     * Determines the size of the tree (retries until the tree can be read in
     * its entirety without concurrent modification)
     * @return the size of the tree
     */
    public final int sizeBlocking() {
        int sz = 0;
        for (;;) if ((sz = size()) != -1) return sz;
    }

    /**
     * This assumes that there are no concurrent accesses occurring.
     * If concurrent accesses can occur, use size() or sizeBlocking().
     */
    public int sequentialSize() {
        return sequentialSize(root);
    }

    /**
     * Clones the tree (retries until the tree can be read in its
     * entirety without concurrent modification)
     * @return a reference to a clone of the tree
     */
    @Override
    public final LockFree4ST clone() {
        Node newroot = null;
        for (;;) if ((newroot = getSnapshot()) != null) return new LockFree4ST(newroot);
    }

    @Override
    public final String toString() {
        StringBuffer sb = new StringBuffer();
        treeString(root, sb);
        return sb.toString();
    }



    /**
     *
     * PRIVATE FUNCTIONS
     *
     */

    // Precondition: `nonnull' is non-null
    private static <E extends Comparable<? super E>, V> boolean less(final E nonnull, final E other) {
        if (other == null) return true;
        return nonnull.compareTo(other) < 0;
    }

    // Precondition: `nonnull' is non-null
    private static <E extends Comparable<? super E>, V> boolean equal(final E nonnull, final E other) {
        if (other == null) return false;
        return nonnull.compareTo(other) == 0;
    }

    private Node<E,V> child(final E key, final Node<E,V> l) {
        if (less(key, l.k0)) return l.c0;
        if (less(key, l.k1)) return l.c1;
        if (less(key, l.k2)) return l.c2;
        return l.c3;

    }

    private void help(final Info<E,V> info) {
        if (info.getClass() == IInfo.class)      helpInsert((IInfo<E,V>) info);
        else if (info.getClass() == DInfo.class) helpDelete((DInfo<E,V>) info);
        else if (info.getClass() == Mark.class)  helpMarked(((Mark<E,V>) info).dinfo);
    }

    private void helpInsert(final IInfo<E,V> info) {
        // CAS the correct child pointer of p from oldchild to newchild
        switch (info.pindex) {                                                  // [[ ichild CAS ]]
            case 0: c0Updater.compareAndSet(info.p, info.oldchild, info.newchild); break;
            case 1: c1Updater.compareAndSet(info.p, info.oldchild, info.newchild); break;
            case 2: c2Updater.compareAndSet(info.p, info.oldchild, info.newchild); break;
            case 3: c3Updater.compareAndSet(info.p, info.oldchild, info.newchild); break;

            default: assert(false); break;
        }
        infoUpdater.compareAndSet(info.p, info, new Clean<E,V>());              // [[ iunflag CAS ]]
    }

    private boolean helpDelete(final DInfo<E,V> info) {
        final boolean markSuccess = infoUpdater.compareAndSet(
                info.p, info.pinfo, new Mark<E,V>(info));                     // [[ mark CAS ]]
        final Info<E,V> currentPInfo = info.p.info;
        if (markSuccess || (currentPInfo.getClass() == Mark.class
                && ((Mark<E,V>) currentPInfo).dinfo == info)) {
            helpMarked(info);
            return true;
        } else {
            help(currentPInfo);
            infoUpdater.compareAndSet(info.gp, info, new Clean<E,V>());       // [[ backtrack CAS ]]
            return false;
        }
    }

    private void helpMarked(final DInfo<E,V> info) {
        // observe that there are two non-empty children of info.p
        // so the following test correctly finds the "other" (remaining) node
        final Node<E,V> other = (info.p.c0.kcount > 0 && info.p.c0 != info.l) ? info.p.c0 :
                                (info.p.c1.kcount > 0 && info.p.c1 != info.l) ? info.p.c1 :
                                (info.p.c2.kcount > 0 && info.p.c2 != info.l) ? info.p.c2 :
                                info.p.c3;


        // CAS the correct child pointer of info.gp from info.p to other
        switch (info.gpindex) {                                                 // [[ dchild CAS ]]
            case 0: c0Updater.compareAndSet(info.gp, info.p, other); break;
            case 1: c1Updater.compareAndSet(info.gp, info.p, other); break;
            case 2: c2Updater.compareAndSet(info.gp, info.p, other); break;
            case 3: c3Updater.compareAndSet(info.gp, info.p, other); break;

            default: assert(false); break;
        }
        infoUpdater.compareAndSet(info.gp, info, new Clean<E,V>());           // [[ dunflag CAS ]]
    }

    public static void treeString(Node root, StringBuffer sb) {
        if (root == null) {
            sb.append("*");
            return;
        }
        sb.append("(");
        sb.append(root.kcount);
        sb.append(" keys,");
        sb.append((root.k0 == null ? "-" : root.k0.toString()));sb.append(",");
        sb.append((root.k1 == null ? "-" : root.k1.toString()));sb.append(",");
        sb.append((root.k2 == null ? "-" : root.k2.toString()));sb.append(",");

        treeString(root.c0, sb); sb.append(",");
        treeString(root.c1, sb); sb.append(",");
        treeString(root.c2, sb); sb.append(",");
        treeString(root.c3, sb); sb.append(")");

    }

    /**
     * This assumes that there are no concurrent accesses occurring!
     * This is more efficient than size(), but its correctness is only
     * guaranteed if it is known that there will be no concurrent accesses
     * to the tree.  If this is not known for certain, then use size() instead.
     * @return the size of the tree
     */
    private int sequentialSize(final Node node) {
        if (node == null) return 0;
        if (node.c0 == null) {
            return (node.k2 != null ? 3 :
                    node.k1 != null ? 2 :
                    node.k0 != null ? 1 :
                    0);
        } else {
            return sequentialSize(node.c0) +
                   sequentialSize(node.c1) +
                   sequentialSize(node.c2) +
                   sequentialSize(node.c3);
        }
    }

    /**
     * Makes a shallow copy of node <code>node</code>, storing information
     * regarding its child references in map <code>refs</code>, before
     * recursively operating on its children.
     */
    private void readRefs(final Node node, final HashMap<Node, Children> refs) {
        if (node == null) return;
        refs.put(node, new Children(node));
        readRefs(node.c0, refs);
        readRefs(node.c1, refs);
        readRefs(node.c2, refs);
        readRefs(node.c3, refs);

    }

    /**
     * Checks that the structure of the subtree rooted at <code>node</code>
     * matches the structure captured in the map <code>refs</code>.
     * @return true if the structures match, and false otherwise
     */
    private boolean checkRefs(final Node node, final HashMap<Node, Children> refs) {
        if (node == null) return true;
        Children children = refs.get(node);
        if (!children.equals(new Children(node))) return false;
        return checkRefs(children.c0, refs) &&
               checkRefs(children.c1, refs) &&
               checkRefs(children.c2, refs) &&
               checkRefs(children.c3, refs);
    }

    /**
     * Record the structure of the tree (child references) in map <code>refs</code>.
     */
    private Node buildRefs(final Node node, final HashMap<Node, Children> refs) {
        if (node == null) return null;
        Children children = refs.get(node);
        return new Node(node, buildRefs(children.c0, refs),
                              buildRefs(children.c1, refs),
                              buildRefs(children.c2, refs),
                              buildRefs(children.c3, refs));
    }

    /**
     * Obtains a snapshot of the tree by reading and duplicating all tree
     * structure, then re-checking all "live" tree structure to see that it
     * matches the recorded structure (to ensure validity of the snapshot),
     * then building the copy from the recorded information.
     * @return a snapshot of the tree, or null if the tree was concurrently modified
     */
    private Node getSnapshot() {
        final HashMap<Node, Children> refs = new HashMap<Node, Children>();
        readRefs(root, refs);
        if (!checkRefs(root, refs)) return null;
        return buildRefs(root, refs);
    }



    /**
     *
     * PRIVATE CLASSES
     *
     */

    public static final class Node<E extends Comparable<? super E>, V> {
        public final int kcount;                   // key count
        public final E k0, k1, k2;                     // keys
        public final V v0, v1, v2;                   // values
        public volatile Node<E,V> c0, c1, c2, c3;        // children
        public volatile Info<E,V> info = null;

        /**
         * DEBUG CODE
         */
        public Node(final Node<E,V> node, final Node c0,
                                          final Node c1,
                                          final Node c2,
                                          final Node c3) {
            this.kcount = node.kcount;
            this.k0 = node.k0;
            this.k1 = node.k1;
            this.k2 = node.k2;

            this.v0 = node.v0;
            this.v1 = node.v1;
            this.v2 = node.v2;

            this.c0 = c0;
            this.c1 = c1;
            this.c2 = c2;
            this.c3 = c3;

        }

        /**
         * Constructor for leaf with zero keys.
         */
        Node() {
            kcount = 0;
            this.k0 = this.k1 = this.k2 = null;
            this.v0 = this.v1 = this.v2 = null;
        }

        /**
         * Constructor for newly created leaves with one key.
         */
        public Node(final E key, final V value) {
            this.k0 = key;
            this.v0 = value;
            this.kcount = 1;

            // fill in the final variables
            this.k1 = this.k2 = null;
            this.v1 = this.v2 = null;
        }

        /**
         * Constructor for the root of the tree.
         *
         * The initial tree consists of 2+4 nodes.
         *   The root node has 1 child, its 3 keys are null (infinity),
         *     and its key count is 3.
         *   The sole child of the root, c0, is an internal node with 4 children.
         *     Its keys are also null, and its key count is 3.
         *     Its children are leaves.  c0 is an empty leaf (no keys).
         *     c1, c2, ... are leaves with 1 key, namely null (representing infinity).
         *     c1, c2, ... exist to prevent deletion of this node (root.c0).
         *
         * @param root if true, the root is created otherwise, if false,
         *             the root's child root.c0 is created.
         */
        public Node(final boolean root) {
            this.k0 = this.k1 = this.k2 = null;                   // fill in final variables
            this.v0 = this.v1 = this.v2 = null;
            this.kcount = 3;
            if (root) {
                c0 = new Node<E,V>(false); // only c0 since other children unused
            } else {
                this.c0 = new Node<E,V>();  // empty leaf
                // more empty leaves -- prevent deletion of this
                this.c1 = new Node<E,V>();this.c2 = new Node<E,V>();this.c3 = new Node<E,V>();
            }
        }

        /**
         * Constructor for case that <code>(knew,vnew)</code> is being inserted,
         * the leaf's key set is full (<code>l.kcount == 3</code>), and knew is
         * not in l. This constructor creates a new internal node with 3 keys and
         * 4 children sorted by key.
         */
        public Node(final E knew, final V vnew, final Node<E,V> l) {
             if (less(knew, l.k0)) {
                this.c0 = new Node<E,V>(knew, vnew);
                this.c1 = new Node<E,V>(l.k0, l.v0);
                this.c2 = new Node<E,V>(l.k1, l.v1);
                this.c3 = new Node<E,V>(l.k2, l.v2);
            } else if (less(knew, l.k1)) {
                this.c0 = new Node<E,V>(l.k0, l.v0);
                this.c1 = new Node<E,V>(knew, vnew);
                this.c2 = new Node<E,V>(l.k1, l.v1);
                this.c3 = new Node<E,V>(l.k2, l.v2);
            } else if (less(knew, l.k2)) {
                this.c0 = new Node<E,V>(l.k0, l.v0);
                this.c1 = new Node<E,V>(l.k1, l.v1);
                this.c2 = new Node<E,V>(knew, vnew);
                this.c3 = new Node<E,V>(l.k2, l.v2);
            } else {
                this.c0 = new Node<E,V>(l.k0, l.v0);
                this.c1 = new Node<E,V>(l.k1, l.v1);
                this.c2 = new Node<E,V>(l.k2, l.v2);
                this.c3 = new Node<E,V>(knew, vnew);
            }

            this.k0 = this.c1.k0;
            this.k1 = this.c2.k0;
            this.k2 = this.c3.k0;

            this.v0 = this.v1 = this.v2 = null;
            this.kcount = 3;
        }

        /**
         * Constructor for case that <code>cnew</code> is being inserted, and
         * the leaf's key set is not full.
         * This constructor creates a new leaf with keycount(old leaf)+1 keys.
         *
         * @param knew the key being inserted
         * @param l the leaf into which the key is being inserted
         * @param haskey indicates whether l already has <code>knew</code> as a key
         */
        public Node(final E knew, final V vnew, final Node<E,V> l, final boolean haskey) {
            if (haskey) {
                if (equal(knew, l.k0)) {
                    this.k0 = knew;
                    this.v0 = vnew;
                    this.k1 = l.k1;
                    this.v1 = l.v1;
                    this.k2 = l.k2;
                    this.v2 = l.v2;
                } else if (equal(knew, l.k1)) {
                    this.k0 = l.k0;
                    this.v0 = l.v0;
                    this.k1 = knew;
                    this.v1 = vnew;
                    this.k2 = l.k2;
                    this.v2 = l.v2;
                } else {
                    this.k0 = l.k0;
                    this.v0 = l.v0;
                    this.k1 = l.k1;
                    this.v1 = l.v1;
                    this.k2 = knew;
                    this.v2 = vnew;
                }
                this.kcount = l.kcount;
            } else {
                if (less(knew, l.k0)) {
                    this.k0 = knew;
                    this.v0 = vnew;
                    this.k1 = l.k0;
                    this.v1 = l.v0;
                    this.k2 = l.k1;
                    this.v2 = l.v1;
                } else if (less(knew, l.k1)) {
                    this.k0 = l.k0;
                    this.v0 = l.v0;
                    this.k1 = knew;
                    this.v1 = vnew;
                    this.k2 = l.k1;
                    this.v2 = l.v1;
                } else {
                    this.k0 = l.k0;
                    this.v0 = l.v0;
                    this.k1 = l.k1;
                    this.v1 = l.v1;
                    this.k2 = knew;
                    this.v2 = vnew;
                }
                this.kcount = l.kcount + 1;
            }

        }

        /**
         * Constructor for the case that a key is being deleted from the
         * key set of a leaf.  This constructor creates a new leaf with
         * keycount(old leaf)-1 sorted keys.
         */
        public Node(final E key, final Node<E,V> l, final int kcount) {
            this.kcount = kcount;
            this.k2 = null;
            this.v2 = null;
             if (equal(key, l.k0)) {
                this.k0 = l.k1;
                this.v0 = l.v1;
                this.k1 = l.k2;
                this.v1 = l.v2;
            } else if (equal(key, l.k1)) {
                this.k0 = l.k0;
                this.v0 = l.v0;
                this.k1 = l.k2;
                this.v1 = l.v2;
            } else {
                this.k0 = l.k0;
                this.v0 = l.v0;
                this.k1 = l.k1;
                this.v1 = l.v1;
            }
        }

        // Precondition: key is not null
        final boolean hasKey(final E key) {
            return equal(key, k0) || equal(key, k1) || equal(key, k2);
        }

        // Precondition: key is not null
        V getValue(E key) {
            return equal(key, k0) ? v0 :
                   equal(key, k1) ? v1 :
                   equal(key, k2) ? v2 : null;
        }

        @Override
        public String toString() {
            StringBuffer sb = new StringBuffer();
            treeString(this, sb);
            return sb.toString();
        }

        @Override
        public final boolean equals(final Object o) {
            if (o == null) return false;
            if (o.getClass() != getClass()) return false;
            Node n = (Node) o;
            if (n.c0 != null && !n.c0.equals(c0) ||
                n.c1 != null && !n.c1.equals(c1) ||
                n.c2 != null && !n.c2.equals(c2) ||
                n.c3 != null && !n.c3.equals(c3)) return false;
            if (n.k0 != null && !n.k0.equals(k0) ||
                n.k1 != null && !n.k1.equals(k1) ||
                n.k2 != null && !n.k2.equals(k2)) return false;
            if (n.v0 != null && !n.v0.equals(v0) ||
                n.v1 != null && !n.v1.equals(v1) ||
                n.v2 != null && !n.v2.equals(v2)) return false;
            if ((n.info != null && !n.info.equals(info)) || n.kcount != kcount) return false;
            return true;
        }

    }

    static interface Info<E extends Comparable<? super E>, V> {}

    static final class IInfo<E extends Comparable<? super E>, V> implements Info<E,V> {
        final Node<E,V> p, oldchild, newchild;
        final int pindex;

        IInfo(final Node<E,V> oldchild, final Node<E,V> p, final Node<E,V> newchild,
                final int pindex) {
            this.p = p;
            this.oldchild = oldchild;
            this.newchild = newchild;
            this.pindex = pindex;
        }

        @Override
        public boolean equals(Object o) {
            IInfo x = (IInfo) o;
            if (x.p != p
                    || x.oldchild != oldchild
                    || x.newchild != newchild
                    || x.pindex != pindex) return false;
            return true;
        }
    }

    static final class DInfo<E extends Comparable<? super E>, V> implements Info<E,V> {
        final Node<E,V> p, l, gp;
        final Info<E,V> pinfo;
        final int gpindex;

        DInfo(final Node<E,V> l, final Node<E,V> p, final Node<E,V> gp,
                final Info<E,V> pinfo, final int gpindex) {
            this.p = p;
            this.l = l;
            this.gp = gp;
            this.pinfo = pinfo;
            this.gpindex = gpindex;
        }

        @Override
        public boolean equals(Object o) {
            DInfo x = (DInfo) o;
            if (x.p != p
                    || x.l != l
                    || x.gp != gp
                    || x.pinfo != pinfo
                    || x.gpindex != gpindex) return false;
            return true;
        }
    }

    static final class Mark<E extends Comparable<? super E>, V> implements Info<E,V> {
        final DInfo<E,V> dinfo;

        Mark(final DInfo<E,V> dinfo) {
            this.dinfo = dinfo;
        }

        @Override
        public boolean equals(Object o) {
            Mark x = (Mark) o;
            if (x.dinfo != dinfo) return false;
            return true;
        }
    }

    static final class Clean<E extends Comparable<? super E>, V> implements Info<E,V> {
        @Override
        public boolean equals(Object o) {
            return (this == o);
        }
    }

    private class Children {
        Node c0, c1, c2, c3;
        public Children(Node node) {
            this.c0 = node.c0;
            this.c1 = node.c1;
            this.c2 = node.c2;
            this.c3 = node.c3;

        }
        @Override
        public boolean equals(Object o) {
            if (o == null || !o.getClass().equals(getClass())) return false; // CAN DO AWAY WITH THIS AT THE COST OF TYPE SAFETY!!
            Children children = (Children) o;
            return this.c0 == children.c0 &&
                   this.c1 == children.c1 &&
                   this.c2 == children.c2 &&
                   this.c3 == children.c3;
        }
    }
}