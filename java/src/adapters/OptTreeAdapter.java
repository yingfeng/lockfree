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
import algorithms.bronson.OptTreeMap;
import org.deuce.transform.Exclude;

/**
 *
 * @author trev
 */
@Exclude
public class OptTreeAdapter<K extends Comparable<? super K>> extends AbstractAdapter<K> implements SetInterface<K> {
    OptTreeMap<K,K> tree = new OptTreeMap<K,K>();
    public OptTreeAdapter() {

    }

    public boolean add(final K key, final Random rng) {
//        return tree.putIfAbsent(key, key) == null;
        return tree.put(key, key) == null;
    }

    public K get(K key) {
        return tree.get(key);
    }

    public boolean remove(final K key, final Random rng) {
        return tree.remove(key) != null;
    }

    public boolean contains(final K key) {
        return tree.containsKey(key);
    }

    public int size() {
        return tree.size();
    }

    public void addListener(OperationListener l) {
        
    }

    public KSTNode<K> getRoot() {
        return tree.getRoot();
    }

    public int getSumOfDepths() {
        return tree.getSumOfDepths();
    }

    public int sequentialSize() {
        return size();
    }
}
