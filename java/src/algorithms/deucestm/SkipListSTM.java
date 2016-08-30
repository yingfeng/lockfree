package algorithms.deucestm;

import java.util.Random;
import main.support.KSTNode;
import org.deuce.Atomic;

/**
 * @author Pascal Felber (modified to use generics, and store key-value pairs by Trevor Brown)
 * @since 0.3
 */
public class SkipListSTM<K,V> {

    /********** BEGIN CODE FOR TREE MEASUREMENT AFTER BENCHMARKS ***********/
    public final KSTNode<K> getKSTRoot() {
        return null;
    }

    public final double averageSearchCost() {
        // compute this using dijkstra's alg.
        return 0;
    }

    public final int sequentialSize() {
        int retval = 0;
        Node<K,V> node = m_head, next;
        while (true) {
            next = node.getForward(0);
            if (compare(next.getValue(), (K) new Integer(Integer.MAX_VALUE)) == 0) break;
            node = next;
            ++retval;
        }
        return retval;
    }
    /********** END CODE FOR TREE MEASUREMENT AFTER BENCHMARKS ***********/    
    
    public class Node<K,V> {
        final private K m_value;
        private V m_value2;
        final private Node<K,V>[] m_forward;

        public Node(int level, K value, V value2) {
            m_value = value;
            m_value2 = value2;
            m_forward = new Node[level + 1];
        }

        public K getValue() {
            return m_value;
        }

        public V getValue2() {
            return m_value2;
        }

        public int getLevel() {
            return m_forward.length - 1;
        }

        public void setForward(int level, Node next) {
            m_forward[level] = next;
        }

        public Node<K,V> getForward(int level) {
            return m_forward[level];
        }

        public String toString() {
            String result = "";
            result += "<l=" + getLevel() + ",v=" + m_value + ">:";
            for (int i = 0; i <= getLevel(); i++) {
                result += " @[" + i + "]=";
                if (m_forward[i] != null) {
                    result += m_forward[i].getValue();
                } else {
                    result += "null";
                }
            }
            return result;
        }
    }
    // Probability to increase level
    final private double m_probability;
    // Upper bound on the number of levels
    final private int m_maxLevel;
    // Highest level so far
    private int m_level;
    // First element of the list
    final private Node m_head;
    // Thread-private PRNG
    final private static ThreadLocal<Random> s_random = new ThreadLocal<Random>() {
        protected synchronized Random initialValue() {
            return new Random();
        }
    };

    public SkipListSTM(int maxLevel, double probability) {
        m_maxLevel = maxLevel;
        m_probability = probability;
        m_level = 0;
        m_head = new Node(m_maxLevel, (K) new Integer(Integer.MIN_VALUE), (V) new Integer(Integer.MIN_VALUE));
        Node tail = new Node(m_maxLevel, (K) new Integer(Integer.MAX_VALUE), (V) new Integer(Integer.MAX_VALUE));
        for (int i = 0; i <= m_maxLevel; i++) {
            m_head.setForward(i, tail);
        }
    }

    public SkipListSTM() {
        this(32, 0.25);
    }

    protected int randomLevel() {
        int l = 0;
        while (l < m_maxLevel && s_random.get().nextDouble() < m_probability) {
            l++;
        }
        return l;
    }
    
    protected int compare(K key1, K key2) {
        if (key1==null) {
            if (key2==null) return 0;
            return key2==null ? 0 : -1;
        } else {
            return ((Comparable) key1).compareTo(key2);
        }
    }

    @Atomic
    public V put(K value, V value2) {
 
        Node[] update = new Node[m_maxLevel + 1];
        Node<K,V> node = m_head;

        for (int i = m_level; i >= 0; i--) {
            Node<K,V> next = node.getForward(i);
            while (compare(next.getValue(), value) < 0) {
                node = next;
                next = node.getForward(i);
            }
            update[i] = node;
        }
        node = node.getForward(0);

        if (compare(node.getValue(), value) == 0) {
            final V retval = node.getValue2();
            node.m_value2 = value2;
            return retval;
        } else {
            int level = randomLevel();
            if (level > m_level) {
                for (int i = m_level + 1; i <= level; i++) {
                    update[i] = m_head;
                }
                m_level = level;
            }
            node = new Node<K,V>(level, value, value2);
            for (int i = 0; i <= level; i++) {
                node.setForward(i, update[i].getForward(i));
                update[i].setForward(i, node);
            }
            return null;
        }
    }

    @Atomic
    public boolean putIfAbsent(K value, V value2) {
 
        Node[] update = new Node[m_maxLevel + 1];
        Node<K,V> node = m_head;

        for (int i = m_level; i >= 0; i--) {
            Node<K,V> next = node.getForward(i);
            while (compare(next.getValue(), value) < 0) {
                node = next;
                next = node.getForward(i);
            }
            update[i] = node;
        }
        node = node.getForward(0);

        if (compare(node.getValue(), value) == 0) {
            final V retval = node.getValue2();
            //node.m_value2 = value2;
            return false;
        } else {
            int level = randomLevel();
            if (level > m_level) {
                for (int i = m_level + 1; i <= level; i++) {
                    update[i] = m_head;
                }
                m_level = level;
            }
            node = new Node<K,V>(level, value, value2);
            for (int i = 0; i <= level; i++) {
                node.setForward(i, update[i].getForward(i));
                update[i].setForward(i, node);
            }
            return true;
        }
    }

    @Atomic
    public V remove(K value) {
        Node[] update = new Node[m_maxLevel + 1];
        Node<K,V> node = m_head;

        for (int i = m_level; i >= 0; i--) {
            Node<K,V> next = node.getForward(i);
            while (compare(next.getValue(), value) < 0) {
                node = next;
                next = node.getForward(i);
            }
            update[i] = node;
        }
        node = node.getForward(0);

        if (compare(node.getValue(), value) != 0) {
            return null;
        } else {
            for (int i = 0; i <= m_level; i++) {
                if (update[i].getForward(i) == node) {
                    update[i].setForward(i, node.getForward(i));
                }
            }
            while (m_level > 0 && m_head.getForward(m_level).getForward(0) == null) {
                m_level--;
            }
            return node.getValue2();
        }
    }

    @Atomic
    public boolean contains(K value) {
        boolean result;

        Node<K,V> node = m_head;
        for (int i = m_level; i >= 0; i--) {
            Node<K,V> next = node.getForward(i);
            while (compare(next.getValue(), value) < 0) {
                node = next;
                next = node.getForward(i);
            }
        }
        node = node.getForward(0);

        result = (compare(node.getValue(), value) == 0);

        return result;
    }

    @Atomic
    public V get(K value) {
        Node<K,V> node = m_head;

        for (int i = m_level; i >= 0; i--) {
            Node<K,V> next = node.getForward(i);
            while (compare(next.getValue(), value) < 0) {
                node = next;
                next = node.getForward(i);
            }
        }
        node = node.getForward(0);
        if (compare(node.getValue(), value) == 0) {
            return node.getValue2();
        } 
        return null;
    }

    public String toString() {
        String result = "";

        result += "Skip list:\n";
        result += "  Level=" + m_level + "\n";
        result += "  Max_level=" + m_maxLevel + "\n";
        result += "  Probability=" + m_probability + "\n";

        result += "Elements:\n";
        int[] countLevel = new int[m_maxLevel + 1];
        Node<K,V> element = m_head.getForward(0);
        while (compare(element.getValue(), (K) new Integer(Integer.MAX_VALUE)) < 0) {
            countLevel[element.getLevel()]++;
            result += "  " + element.toString() + "\n";
            element = element.getForward(0);
        }

        result += "Level distribution:\n";
        for (int i = 0; i <= m_maxLevel; i++) {
            result += "  #[" + i + "]=" + countLevel[i] + "\n";
        }

        return result;
    }
}
