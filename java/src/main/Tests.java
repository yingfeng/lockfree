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

package main;

import java.awt.BasicStroke;
import java.awt.Graphics2D;
import java.awt.RenderingHints;
import java.awt.image.BufferedImage;
import java.io.File;
import javax.imageio.ImageIO;
import main.support.KSTNode;
import main.support.KSTNodeTools;
import main.support.TreeRenderer;

/**
 *
 * @author trbot
 */
public class Tests {
    
    
    public static void renderTree(KSTNode<Integer> tree, String png_name, boolean drawText) {
        renderTree(tree, 3000, 1500, png_name, png_name, drawText, 18);
    }
    public static void renderTree(KSTNode<Integer> tree, int w, int h, String png_name, boolean drawText) {
        renderTree(tree, w, h, png_name, png_name, drawText, 18);
    }
    public static void renderTree(KSTNode<Integer> tree, int w, int h, String png_name, String title, boolean drawText) {
        renderTree(tree, w, h, png_name, title, drawText, 18);
    }
    public synchronized static void renderTree(KSTNode<Integer> tree, int w, int h, String png_name, String title, boolean drawText, float fontSize) {
        TreeRenderer<Integer> renderer = new TreeRenderer<>(drawText, fontSize);
        BufferedImage img = renderer.render(tree, w, h);
            Graphics2D g = img.createGraphics();
            g.setRenderingHint(RenderingHints.KEY_TEXT_ANTIALIASING, RenderingHints.VALUE_TEXT_ANTIALIAS_ON);
            g.setRenderingHint(RenderingHints.KEY_ANTIALIASING, RenderingHints.VALUE_ANTIALIAS_ON);
            g.setFont(g.getFont().deriveFont((float) fontSize));
//            g.setColor(Color.getHSBColor(49f/360f, 0.40f, 0.34f));
//            g.setFont(Font.decode("Calibri-" + (int) fontSize));
            if (title != null) g.drawString(title, 10, 34);
            g.drawString("Average key depth: " +
                         (int) (KSTNodeTools.averageKeyDepth(tree)+0.5),
                         10,
                         70);
            //g.setColor(Color.BLACK);
            g.setStroke(new BasicStroke(4.0f));
            g.drawRect(0, 0, img.getWidth() - 1, img.getHeight() - 1);
            String filename = "output_images/" + png_name + ".png";
        try {
            ImageIO.write(img, "png", new File(filename));
        } catch (Exception ex) {
            ex.printStackTrace();
            System.exit(-1);
        }
        System.out.println("wrote image " + filename);
    }
}
