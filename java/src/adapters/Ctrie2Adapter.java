/**
 * Java test harness for throughput experiments on concurrent data structures.
 * Copyright (C) 2012 Trevor Brown
 * Contact (me [at] tbrown [dot] pro) with any questions or comments.
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

package adapters;

import main.support.SetInterface;
import main.support.KSTNode;
import main.support.OperationListener;
import main.support.Random;
import ctries2.*;
import java.util.ArrayList;
import scala.collection.*;
import scala.collection.immutable.ListMap;

/**
 *
 * @author trev
 */
public class Ctrie2Adapter<K extends Comparable<? super K>> extends AbstractAdapter<K> implements SetInterface<K> {
    public final ConcurrentTrie<K,K> tree;
    KSTNode<K> root;
    
    public Ctrie2Adapter() {
        tree = new ConcurrentTrie<K,K>();
    }

    public final boolean contains(final K key) {
        return tree.lookup(key) != null;
    }
    
    public final boolean add(final K key, final Random rng) {
        return add(key, rng, null);
    }

    @Override
    public final boolean add(final K key, final Random rng, final int[] metrics) {
//        return tree.putIfAbsent(key, key).isEmpty();
        return tree.put(key, key).isEmpty();
    }

    public final K get(final K key) {
        return tree.lookup(key);
    }

    public final boolean remove(final K key, final Random rng) {
        return remove(key, rng, null);
    }

    @Override
    public final boolean remove(final K key, final Random rng, final int[] metrics) {
        return !tree.remove(key).isEmpty();
    }

    @Override
    public final Object partialSnapshot(final int _size, final Random rng) {
        final int size = _size / 2; // to match a RangeQuery(rand, rand+_size) in a half full key range, we only want to snapshot the first _size/2 keys
        final Object[] result = new Object[size];
        final Iterator it = tree.iterator();
        int i = 0;
        while (i < size && it.hasNext()) {
            result[i++] = it.next();
        }
        return result;
    }

    public final void addListener(final OperationListener l) {
        //tree.addListener(l);
    }

    public final int size() {
        return tree.size();
    }

    public final int sequentialSize() {
        return tree.size();
    }

    public final KSTNode<K> getRoot() {
        if (root == null) return (root = getRoot((BasicNode) tree.root()));
        return root;
//        return null; // to speed up tests
    }

    private KSTNode<K> getRoot(BasicNode node) {
        if (node == null) return null;
        if (node instanceof INode) {//node.getClass().equals(INode.class.getClass())) {
            INode u = (INode) node;
            return new KSTNode<K>(null, 0, Integer.toHexString(u.hashCode()), getRoot(u.mainnode));
        } else if (node instanceof TNode) {
            TNode u = (TNode) node;
            return new KSTNode<K>((K) u.k(), 0, Integer.toHexString(u.hashCode()));
        } else if (node instanceof SNode) {
            SNode u = (SNode) node;
            return new KSTNode<K>((K) u.k(), 0, Integer.toHexString(u.hashCode()));
        } else if (node instanceof LNode) {
            LNode u = (LNode) node;
            ListMap lm = u.listmap();
            scala.collection.Iterator keys = lm.keysIterator();
            ArrayList<K> arr = new ArrayList<K>();
            while (keys.hasNext()) {
                arr.add((K) keys.next());
            }
            return new KSTNode<K>(arr, arr.size(), 0, Integer.toHexString(u.hashCode()));
        } else if (node instanceof CNode) {
            CNode u = (CNode) node;
            KSTNode[] arr = new KSTNode[u.array().length];
            for (int i=0;i<u.array().length;i++) {
                arr[i] = getRoot(u.array()[i]);
            }
            return new KSTNode<K>(null, 0, Integer.toHexString(u.hashCode()), arr);
        }
        assert false;
        return null;
    }

    // returns the sum of depths of all keys
    private int sumDepths(KSTNode<K> node, int depth) {
        if (node == null) return 0;
        int result = depth*node.keys.size();
        for (int i=0;i<node.children.size();i++) {
            result += sumDepths(node.children.get(i), depth+1);
        }
        return result;
//        return 0; // to speed up tests
    }

    public final int getSumOfDepths() {
        return sumDepths(getRoot(), 0);
    }

    @Override
    public String toString() {
        return tree.toString();
    }

    @Override
    public final Object rangeQuery(final K lo, final K hi, final int rangeSize, final Random rng) {
        throw new UnsupportedOperationException("Not supported yet.");
    }

}
