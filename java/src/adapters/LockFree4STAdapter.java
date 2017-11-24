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
import algorithms.published.LockFree4ST;

/**
 *
 * @author trev
 */
public class LockFree4STAdapter<K extends Comparable<? super K>> extends AbstractAdapter<K> implements SetInterface<K> {
    LockFree4ST<K,K> tree = new LockFree4ST<>();

    public boolean contains(K key) {
        return tree.containsKey(key);
    }
    
    public boolean add(K key, Random rng) {
        return add(key, rng, null);
    }

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

    public boolean remove(K key, Random rng, final int[] metrics) {
        return tree.remove(key) != null;
    }

    public void addListener(OperationListener l) {
        
    }

    public int size() {
        return tree.size();
    }

    public KSTNode<K> getRoot() {
        return null;
    }

    public int getSumOfDepths() {
        return -1;
    }

    public int sequentialSize() {
        return tree.sequentialSize();
    }

    @Override
    public String toString() {
        return tree.toString();
    }

}
