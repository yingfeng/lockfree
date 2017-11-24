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
import main.support.NoPrefillStructure;
import org.deuce.transform.Exclude;

/**
 *
 * @author trev
 */
@Exclude
public class DummyAdapter<K extends Comparable<? super K>>
extends AbstractAdapter<K>
implements SetInterface<K>, NoPrefillStructure {
    volatile int data;

    public DummyAdapter(double rebalance_probability) {
        
    }

    public boolean contains(K key) {
        return data == (int) (Integer) key;
    }
    
    public boolean add(K key, Random rng) {
        data = (Integer) key;
        return true;
    }

    public K get(K key) {
        return (K) (Integer) data;
    }

    public boolean remove(K key, Random rng) {
        data = (Integer) key;
        return true;
    }

    public void addListener(OperationListener l) {
//        tree.addListener(l);
    }

    public int size() {
        return 1;
    }

    public KSTNode<K> getRoot() {
        return null;
    }
    
    public double getAverageDepth() {
        return 1;//tree.averageSearchCost();//tree.getSumOfDepths() / (double) tree.getNumberOfNodes();
    }

    public int getSumOfDepths() {
        return 0;//tree.getSumOfDepths();
    }

    public int sequentialSize() {
        return 1;//tree.sequentialSize();
    }

    public double getRebalanceProbability() {
        return 1;//tree.REBALANCE_PROBABILITY;
    }

    @Override
    public String toString() {
        return String.valueOf(data);//tree.toString();
    }
    
//    public void disableRotations() {
//        tree.debugNoRotations = true;
//    }
//
//    public void enableRotations() {
//        tree.debugNoRotations = false;
//    }

}
