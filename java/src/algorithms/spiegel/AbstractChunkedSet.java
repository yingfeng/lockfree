package algorithms.spiegel;

import java.util.AbstractSet;
import java.util.NavigableSet;
import org.deuce.transform.Exclude;

@Exclude
public abstract class AbstractChunkedSet<E> extends AbstractSet<E> implements NavigableSet<E> {

    abstract int expectedNodeSize();
    
}