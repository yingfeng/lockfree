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

import main.support.BBSTInterface;
import main.support.KSTNode;
import main.support.OperationListener;
import main.support.Random;
import algorithms.published.ConcurrentChromaticTreeMap;

///**
// *
// * @author trev
// */
//@Exclude
//public class BBST3LLXCountViolAdapter<K extends Comparable<? super K>> extends AbstractAdapter<K> implements BBSTInterface<K> {
//    public BBST3LLXCountViol<K,K> tree;
//
//    public BBST3LLXCountViolAdapter() {
//        tree = new BBST3LLXCountViol();
//    }
//
//    public BBST3LLXCountViolAdapter(final int allowedViolations) {
//        tree = new BBST3LLXCountViol(allowedViolations);
//    }
//
//    public boolean contains(K key) {
//        return tree.containsKey(key);
//    }
//    
//    public boolean add(K key, Random rng) {
////        return tree.putIfAbsent(key, key) == null;
//        tree.put(key, key); return true;
//    }
//
//    public K get(K key) {
//        return tree.get(key);
//    }
//
//    public boolean remove(K key, Random rng) {
//        return tree.remove(key) != null;
//    }
//
//    public void addListener(OperationListener l) {
//        tree.addListener(l);
//    }
//
//    public int size() {
//        return sequentialSize();
//    }
//
//    public KSTNode<K> getRoot() {
//        return tree.getRoot();
//    }
//    
//    public double getAverageDepth() {
//        return tree.getSumOfDepths() / (double) tree.getNumberOfNodes();
//    }
//
//    public int getSumOfDepths() {
//        return tree.getSumOfDepths();
//    }
//
//    public int sequentialSize() {
//        return tree.size();
//    }
//
//    public double getRebalanceProbability() {
//        return -1;
//    }
//
//    @Override
//    public String toString() {
//        return tree.toString();
//    }
//    
//    public void disableRotations() {
////        tree.debugNoRotations = true;
//    }
//
//    public void enableRotations() {
//      tree.debugNoRotations = false;
//    }
//
//}



public class LockFreeChromaticAdapter<K extends Comparable<? super K>> extends AbstractAdapter<K> implements BBSTInterface<K> {
    public ConcurrentChromaticTreeMap<K,K> tree;

    public LockFreeChromaticAdapter() {
        tree = new ConcurrentChromaticTreeMap();
    }

    public LockFreeChromaticAdapter(final int allowedViolations) {
        tree = new ConcurrentChromaticTreeMap(allowedViolations);
    }

    public boolean contains(K key) {
        return tree.containsKey(key);
    }
    
    public boolean add(K key, Random rng) {
        return tree.putIfAbsent(key, key) == null;
        //tree.put(key, key); return true;
    }

    public K get(K key) {
        return tree.get(key);
    }

    public boolean remove(K key, Random rng) {
        return tree.remove(key) != null;
    }

    public void addListener(OperationListener l) {

    }

    public int size() {
        return sequentialSize();
    }

    public KSTNode<K> getRoot() {
        return null;
    }
    
    public double getAverageDepth() {
        return tree.getSumOfDepths() / (double) tree.getNumberOfNodes();
    }

    public int getSumOfDepths() {
        return tree.getSumOfDepths();
    }

    public int sequentialSize() {
        return tree.size();
    }
    
    public boolean supportsKeysum() {
        return true;
    }
    public long getKeysum() {
        return tree.getKeysum();
    }

    public double getRebalanceProbability() {
        return -1;
    }

    @Override
    public String toString() {
        return tree.toString();
    }
    
    public void disableRotations() {

    }

    public void enableRotations() {

    }

}
