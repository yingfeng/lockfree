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

import algorithms.published.LockFreeBSlackTreeMap;
import main.support.SetInterface;
import main.support.KSTNode;
import main.support.OperationListener;
import main.support.Random;

/**
 *
 * @author trev
 */
public class LockFreeBSlackTreeAdapter<K extends Comparable<? super K>> extends AbstractAdapter<K> implements SetInterface<K> {
    public LockFreeBSlackTreeMap<K,K> tree;

    public LockFreeBSlackTreeAdapter() {
        tree = new LockFreeBSlackTreeMap<>((K) (Integer) 0 /* any key */);
    }

    public LockFreeBSlackTreeAdapter(final int nodeCapacity) {
        tree = new LockFreeBSlackTreeMap<>(nodeCapacity, (K) (Integer) 0 /* any key */);
    }
    
    public boolean contains(final K key) {
        return tree.containsKey(key);
    }

    public boolean add(final K key, final Random rng) {
        return tree.put(key, key) == null;
//        return tree.putIfAbsent(key, key);
    }

    public K get(K key) {
        return tree.get(key);
    }

    public boolean remove(final K key, final Random rng) {
        return tree.remove(key) != null;
    }

    public void addListener(OperationListener l) {

    }

    public int size() {
        return sequentialSize();
    }

    public KSTNode<K> getRoot() {
        return tree.getRoot();
    }

    public int getSumOfDepths() {
        return tree.getSumOfKeyDepths();
    }

    public int sequentialSize() {
        return tree.sequentialSize();
    }
    
    public long getSumOfKeys() {
        return tree.getSumOfKeys();
    }
    
    public boolean supportsKeysum() {
        return true;
    }
    public long getKeysum() {
        return tree.getSumOfKeys();
    }
    
    public void debugPrint(){ 
        tree.debugPrint();
    }
}
