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

import algorithms.deucestm.RBTreeUnsync;
import java.util.Iterator;
import main.support.SetInterface;
import main.support.KSTNode;
import main.support.OperationListener;
import main.support.Random;
import main.support.STMStructure;
import main.support.SequentialStructure;
import org.deuce.transform.Exclude;

/**
 *
 * @author trev
 */
@Exclude
public class RBTreeUnsyncAdapter<K extends Comparable<? super K>>
extends AbstractAdapter<K>
implements SetInterface<K>, SequentialStructure {
    RBTreeUnsync<K,K> tree;

    public RBTreeUnsyncAdapter(double rebalance_probability) {
        tree = new RBTreeUnsync();
    }

    public boolean contains(K key) {
        return tree.contains(key);
    }
    
    public boolean add(K key, Random rng) {
//        return tree.putIfAbsent(key, key);
        return tree.put(key, key) == null;
    }

    public K get(K key) {
        return tree.get(key);
    }

    public boolean remove(K key, Random rng) {
        return tree.remove(key) != null;
    }

    public void addListener(OperationListener l) {
//        tree.addListener(l);
    }

    public int size() {
        return tree.sequentialSize();
    }

    public KSTNode<K> getRoot() {
        return tree.getKSTRoot();
    }
    
    public double getAverageDepth() {
        return tree.getSumOfDepths() / (double) tree.getNumberOfNodes();
    }

    public int getSumOfDepths() {
        return tree.getSumOfDepths();
    }

    public int sequentialSize() {
        int i = 0;
        Iterator it = tree.iterator();
        while (it.hasNext()) {
            ++i;
            it.next();
        }
        return i;
    }

    public double getRebalanceProbability() {
        return 1;//tree.REBALANCE_PROBABILITY;
    }

    @Override
    public String toString() {
        return tree.toString();
    }
    
//    public void disableRotations() {
//        tree.debugNoRotations = true;
//    }
//
//    public void enableRotations() {
//        tree.debugNoRotations = false;
//    }

}
