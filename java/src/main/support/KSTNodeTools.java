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

package main.support;

import org.deuce.transform.Exclude;

/**
 *
 * @author trev
 */
@Exclude
public class KSTNodeTools {

    public static <K> double averageKeyDepth(KSTNode<K> root) {
        return sumOfKeyDepths(root, 0) / (double) countOfKeys(root);
    }

    private static <K> int countOfKeys(KSTNode<K> root) {
        if (root == null) return 0;
        if (root.children == null || root.children.size() == 0) return root.keyCount;
        int count = 0;
        for (KSTNode<K> child : root.children) {
            count += countOfKeys(child);
        }
        return count;

    }

    private static <K> int sumOfKeyDepths(KSTNode<K> root, int depth) {
        if (root == null) return 0;
        if (root.children == null || root.children.size() == 0) return depth * root.keyCount;
        int sum = 0;
        for (KSTNode<K> child : root.children) {
            if (child != null) sum += sumOfKeyDepths(child, 1+depth);
        }
        return sum;
    }
    
}
