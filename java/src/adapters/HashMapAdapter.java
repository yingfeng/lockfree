/**
 * Java test harness for throughput experiments on concurrent data structures.
 * Copyright (C) 2012 Trevor Brown
 * Contact (tabrown [at] cs [dot] toronto [dot edu]) with any questions or comments.
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

import java.util.HashMap;
import main.support.BBSTInterface;
import main.support.KSTNode;
import main.support.OperationListener;
import main.support.Random;
import main.support.SequentialStructure;

/**
 *
 * @author trev
 */
public class HashMapAdapter<K> extends AbstractAdapter<K> implements BBSTInterface<K>, SequentialStructure {
    HashMap<K,K> tree = new HashMap<K,K>();

    public boolean contains(final K key) {
        return tree.containsKey(key);
    }

    public boolean add(final K key, final Random rng) {
        // WARNING: THIS DOES NOT HAVE PUTIFABSENT!!!!!!!
        return (tree.containsKey(key) ? false : tree.put(key, key) == null);
        //tree.put(key, key); return true;
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
