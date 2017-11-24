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

import algorithms.spiegel.ConcurrentSkipTreeMap;
import main.support.SetInterface;
import main.Globals;
import main.support.KSTNode;
import main.support.OperationListener;
import main.support.Random;
import java.util.*;
import java.util.concurrent.ConcurrentNavigableMap;
import org.deuce.transform.Exclude;

/**
 *
 * @author trev
 */
@Exclude
public class SkipTreeAdapter<K> extends AbstractAdapter<K> implements SetInterface<K> {
    final ConcurrentSkipTreeMap<K,K> tree = new ConcurrentSkipTreeMap<K,K>();

    public final boolean contains(final K key) {
        return tree.containsKey(key);
    }
    
    public final boolean add(final K key, final Random rng) {
//        return tree.putIfAbsent(key, key) == null;
        return tree.put(key, key) == null;
    }

    public final K get(final K key) {
        return tree.get(key);
    }

    public final boolean remove(final K key, final Random rng) {
        return tree.remove(key) != null;
    }

    @Override
    public final Object rangeQuery(final K lo, final K hi, final int rangeSize, final Random rng) {
        assert rangeSize == Globals.DEFAULT_RQ_SIZE;
        final Object[] result = new Object[rangeSize];
        final ConcurrentNavigableMap map = tree.subMap(lo, true, hi, true);
        final Iterator<K> it = map.keySet().iterator();
        int i = 0;
        while (it.hasNext()) {
            result[i++] = it.next();
        }
        return result;
    }

    @Override
    public final Object partialSnapshot(final int size, final Random rng) {
        throw new RuntimeException("not implemented");
    }

    public final void addListener(final OperationListener l) {

    }

    public final int size() {
        return tree.size();
    }

    public final KSTNode<K> getRoot() {
        return null;
    }

    public final int getSumOfDepths() {
        return 0;
    }

    public final int sequentialSize() {
        return tree.size();
    }
}
