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

import java.util.Collections;
import java.util.Map;
import main.support.SetInterface;
import main.support.KSTNode;
import main.support.OperationListener;
import main.support.Random;
import main.support.SequentialPrefillStructure;
import java.util.TreeMap;

/**
 *
 * @author trev
 */
public class SyncTreeMapAdapter<K> extends AbstractAdapter<K> implements SetInterface<K>, SequentialPrefillStructure {
    final Map<K,K> tree = Collections.synchronizedMap(new TreeMap<K,K>());

    public boolean contains(final K key) {
        return tree.containsKey(key);
    }

    public boolean add(final K key, final Random rng) {
        return tree.put(key, key) == null;
//        // WARNING: THIS DOES NOT NATIVELY HAVE PUTIFABSENT
//        // simulate putIfAbsent
//        synchronized (tree) {
//            return (tree.containsKey(key) ? false : tree.put(key, key) == null);
//            // note: we need the sync block to make the containsKey() AND put() atomic (together)
//            // this lets us implement atomic putIfAbsent
//        }
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
        return tree.size();
    }

    public KSTNode<K> getRoot() {
        return null;
    }

    public int getSumOfDepths() {
        return 0;
    }

    public int sequentialSize() {
        return size();
    }
}
