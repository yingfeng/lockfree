package main.support;


import java.awt.Color;
import java.awt.Font;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.geom.AffineTransform;
import java.awt.image.BufferedImage;
import java.util.ArrayList;

public class TreeRenderer<K extends Comparable<? super K>> {

    public final static boolean DRAW_ADDRESSES = true;
    public final static boolean DRAW_WEIGHTS = true;
    public final static int IMAGE_PADDING = 30;//75;
    public final static int TEXT_WIDTH = 500;
    public final static int NODE_RADIUS = 2;
    public final static int TEXT_PADDING_X = 5;
    public final static int TEXT_PADDING_Y = 5;

    private final boolean DRAW_TEXT;

    private double fontSize;

    public TreeRenderer(boolean drawText) {
        this(drawText, 10);        
    }

    public TreeRenderer(boolean drawText, double fontSize) {
        this.DRAW_TEXT = drawText;
        this.fontSize = fontSize;
    }

    public void setFontSize(double fontSize) {
        this.fontSize = fontSize;
    }

    public double getFontSize() {
        return fontSize;
    }

    private TreeVisNode<K> constructTree(KSTNode<K> root, int depth) {
        if (root == null) return null;
        TreeVisNode<K> newRoot = new TreeVisNode<>(root.keys,
                                                    root.keyCount,
                                                    root.weight,
                                                    0 /* x value */,
                                                    depth,
                                                    root.address);
        for (KSTNode<K> child : root.children) {
            newRoot.children.add(constructTree(child, depth+1));
        }
        return newRoot;
    }

    private void setXValues(TreeVisNode<K> root, Accumulator acc) {
        if (root == null) return;
        
        // traverse depth first
        boolean hasChildren = false;
        for (KSTNode<K> child : root.children) {
            if (child != null) {
                setXValues((TreeVisNode<K>) child, acc);
                hasChildren = true;
            }
        }
        
        if (!hasChildren) {
            // use x value from accumulator
            root.x = acc.get();
        } else {
            // take average of x values of non-null children
            float nonNullSum = 0;
            int nonNullCount = 0;
            for (KSTNode<K> child : root.children) {
                if (child != null) {
                    nonNullSum += ((TreeVisNode<K>) child).x;
                    nonNullCount++;
                }
            }
            root.x = nonNullSum / nonNullCount;
        }
    }
    
    private float maxx(TreeVisNode<K> root) {
        if (root == null) return -1;
        float x = root.x;
        for (KSTNode<K> child : root.children) {
            x = Math.max(x, maxx((TreeVisNode<K>) child));
        }
        return x;
    }

    private float maxy(TreeVisNode<K> root) {
        if (root == null) return -1;
        float y = 0;
        for (KSTNode<K> child : root.children) {
            y = Math.max(y, maxy((TreeVisNode<K>) child));
        }
        return y+1;
    }

    private void drawNode(TreeVisNode<K> root, float xstep, float ystep, Graphics2D g) {
        int x = Math.round(root.x*xstep);
        int y = Math.round(root.y*ystep);

        g.setColor(Color.WHITE);
        g.fillOval(x-NODE_RADIUS, y-NODE_RADIUS, NODE_RADIUS*2+1, NODE_RADIUS*2+1);
        g.setColor(Color.BLACK);
        g.drawOval(x-NODE_RADIUS, y-NODE_RADIUS, NODE_RADIUS*2+1, NODE_RADIUS*2+1);

        if (!DRAW_TEXT) return;
        String s = "";
        if (root.keys.size() > 0) s = root.keys.get(0) + "";
        for (int i=1;i<root.keys.size();i++) s += "," + root.keys.get(i);
        //s += " (" + root.weight + ") : " + root.keyCount;
        if (DRAW_WEIGHTS && root.weight != -1) s += ":" + root.weight;
        
        AffineTransform orig = g.getTransform();
        g.translate(x, y);
        if (root.children.isEmpty()) g.rotate(Math.PI/4);
        g.drawString(s, TEXT_PADDING_X, TEXT_PADDING_Y);
        g.setTransform(orig);
        
        if (DRAW_ADDRESSES) g.drawString(root.address, x+TEXT_PADDING_X, y-10);
    }
    
    private void drawEdge(TreeVisNode<K> parent, TreeVisNode<K> child, float xstep, float ystep, Graphics2D g) {
        g.setColor(Color.BLACK);
        g.drawLine(Math.round(parent.x*xstep), Math.round(parent.y*ystep),
                   Math.round(child.x*xstep), Math.round(child.y*ystep));
    }

    private void drawNodes(TreeVisNode<K> root, float xstep, float ystep, Graphics2D g) {
        if (root == null) return;
        drawNode(root, xstep, ystep, g);
        for (KSTNode<K> child : root.children) {
            drawNodes((TreeVisNode<K>) child, xstep, ystep, g);
        }
    }

    private void drawEdges(TreeVisNode<K> root, float xstep, float ystep, Graphics2D g) {
        if (root == null) return;

        for (KSTNode<K> child : root.children) {
            if (child != null) {
                drawEdge(root, (TreeVisNode<K>) child, xstep, ystep, g);
            }
        }
        for (KSTNode<K> child : root.children) {
            drawEdges((TreeVisNode<K>) child, xstep, ystep, g);
        }
    }

    public BufferedImage render(SetInterface<K> tree, int width, int height) {
        return render(tree.getRoot(), width, height);
    }
    
    public BufferedImage render(KSTNode<K> kstroot, int width, int height) {
        TreeVisNode<K> root = constructTree(kstroot, 0); // (sets y values)
        setXValues(root, new Accumulator());

        int w = width+2*IMAGE_PADDING, h = height+2*IMAGE_PADDING;
        int canvasWidth = w+TEXT_WIDTH;
        int canvasHeight = h+TEXT_WIDTH;
        BufferedImage img = new BufferedImage(canvasWidth, canvasHeight, BufferedImage.TYPE_INT_BGR);
        Graphics2D g = (Graphics2D) img.getGraphics();
        g.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING,
                           RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
        g.setRenderingHint(RenderingHints.KEY_ANTIALIASING,
                           RenderingHints.VALUE_ANTIALIAS_ON);
        
        Font f = g.getFont();   // set font size
        g.setFont(f.deriveFont((float) fontSize));

        g.setBackground(Color.WHITE);
        g.clearRect(0, 0, canvasWidth, canvasHeight);
        g.translate(IMAGE_PADDING, IMAGE_PADDING);
        
        float ystep = height / (maxy(root)-1);
        float xstep = width / maxx(root);
        
        drawEdges(root, xstep, ystep, g);
        drawNodes(root, xstep, ystep, g);
        
        return img;
    }

    private class Accumulator {
        private int count;
        public int get() {
            return count++;
        }
    }

    private class TreeVisNode<K> extends KSTNode<K> {
        float x, y;
        public TreeVisNode(final ArrayList<K> keys,
                           final int keyCount,
                           final long weight,
                           final float x,
                           final float y,
                           final String address,
                           TreeVisNode<K>... children) {
            super(keys, keyCount, weight, address, children);
            this.x = x;
            this.y = y;
        }
    }

}
