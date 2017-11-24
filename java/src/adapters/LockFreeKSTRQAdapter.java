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
import main.Globals;
import main.support.KSTNode;
import main.support.OperationListener;
import main.support.Random;
import algorithms.published.LockFreeKSTRQ;

/**
 *
 * @author trev
 */
public class LockFreeKSTRQAdapter<K extends Comparable<? super K>> extends AbstractAdapter<K> implements SetInterface<K> {
    LockFreeKSTRQ<K,K> tree;
    
    public LockFreeKSTRQAdapter(int k) {
        tree = new LockFreeKSTRQ<K,K>(k);
    }

    public boolean contains(K key) {
        return tree.containsKey(key);
    }
    
    public boolean add(K key, Random rng) {
        return add(key, rng, null);
    }

    @Override
    public boolean add(K key, Random rng, final int[] metrics) {
//        return tree.putIfAbsent(key, key) == null;
        return tree.put(key, key) == null;
    }

    public K get(K key) {
        return tree.get(key);
    }

    public boolean remove(K key, Random rng) {
        return remove(key, rng, null);
    }

    @Override
    public boolean remove(K key, Random rng, final int[] metrics) {
        return tree.remove(key) != null;
    }

    @Override
    public Object rangeQuery(K lo, K hi, int rangeSize, Random rng) {
        return tree.subSet(lo, hi);
    }

    @Override
    public final Object partialSnapshot(final int size, final Random rng) {
        assert size == Globals.DEFAULT_RQ_SIZE;
        Object[] result = new Object[size];
        LockFreeKSTRQ.Node[] leaves = tree.snapshotHeadKeys(size);
        int numKeys = 0;
        for (int i=0;i<leaves.length;i++) {
            final int kcount = leaves[i].kcount;
            if (numKeys + kcount < size) {
                System.arraycopy(leaves[i].k, 0, result, numKeys, kcount);
                numKeys += kcount;
            } else {
                System.arraycopy(leaves[i].k, 0, result, numKeys, size-numKeys);
                break;
            }
        }
        return result;
    }

    public void addListener(OperationListener l) {
        //tree.addListener(l);
    }

    public int size() {
        return tree.size();
    }

    public KSTNode<K> getRoot() {
        return null; //tree.getRoot();
    }

    public int getSumOfDepths() {
        return tree.getSumOfDepths();
    }

    public int sequentialSize() {
        return tree.size();
    }

    @Override
    public String toString() {
        return tree.toString();
    }

}
