/* BasicListUI.java
   Copyright (C) 2002, 2004 Free Software Foundation, Inc.

This file is part of GNU Classpath.

GNU Classpath is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU Classpath is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Classpath; see the file COPYING.  If not, write to the
Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
02111-1307 USA.

Linking this library statically or dynamically with other modules is
making a combined work based on this library.  Thus, the terms and
conditions of the GNU General Public License cover the whole
combination.

As a special exception, the copyright holders of this library give you
permission to link this library with independent modules to produce an
executable, regardless of the license terms of these independent
modules, and to copy and distribute the resulting executable under
terms of your choice, provided that you also meet, for each linked
independent module, the terms and conditions of the license of that
module.  An independent module is a module which is not derived from
or based on this library.  If you modify this library, you may extend
this exception to your version of the library, but you are not
obligated to do so.  If you do not wish to do so, delete this
exception statement from your version. */

package javax.swing.plaf.basic;

import java.awt.Color;
import java.awt.Component;
import java.awt.Dimension;
import java.awt.Graphics;
import java.awt.Point;
import java.awt.Rectangle;
import java.awt.event.FocusEvent;
import java.awt.event.FocusListener;
import java.awt.event.MouseEvent;
import java.beans.PropertyChangeEvent;
import java.beans.PropertyChangeListener;
import javax.swing.JComponent;
import javax.swing.JList;
import javax.swing.ListCellRenderer;
import javax.swing.ListModel;
import javax.swing.ListSelectionModel;
import javax.swing.UIDefaults;
import javax.swing.UIManager;
import javax.swing.event.ListDataEvent;
import javax.swing.event.ListDataListener;
import javax.swing.event.ListSelectionEvent;
import javax.swing.event.ListSelectionListener;
import javax.swing.event.MouseInputListener;
import javax.swing.plaf.ComponentUI;
import javax.swing.plaf.ListUI;


/**
 * The Basic Look and Feel UI delegate for the 
 * JList.
 */
public class BasicListUI extends ListUI
{
  /**
   * A helper class which listens for {@link FocusEvents}
   * from the JList.
   */
  class FocusHandler implements FocusListener
  {
    /**
     * Called when the JList acquires focus.
     *
     * @param e The FocusEvent representing focus acquisition
     */
    public void focusGained(FocusEvent e)
    {
      repaintCellFocus();
    }

    /**
     * Called when the JList loses focus.
     *
     * @param e The FocusEvent representing focus loss
     */
    public void focusLost(FocusEvent e)
    {
      repaintCellFocus();
    }

    /**
     * Helper method to repaint the focused cell's 
     * lost or acquired focus state.
     */
    void repaintCellFocus()
    {
    }
  }

  /**
   * A helper class which listens for {@link ListDataEvent}s generated by
   * the {@link JList}'s {@link ListModel}.
   *
   * @see javax.swing.JList#model
   */
  class ListDataHandler implements ListDataListener
  {
    /**
     * Called when a general change has happened in the model which cannot
     * be represented in terms of a simple addition or deletion.
     *
     * @param e The event representing the change
     */
    public void contentsChanged(ListDataEvent e)
    {
      // System.err.println(this + ".contentsChanged(" + e + ")");
      BasicListUI.this.damageLayout();
    }

    /**
     * Called when an interval of objects has been added to the model.
     *
     * @param e The event representing the addition
     */
    public void intervalAdded(ListDataEvent e)
    {
      // System.err.println(this + ".intervalAdded(" + e + ")");
      BasicListUI.this.damageLayout();
    }

    /**
     * Called when an inteval of objects has been removed from the model.
     *
     * @param e The event representing the removal
     */
    public void intervalRemoved(ListDataEvent e)
    {
      // System.err.println(this + ".intervalRemoved(" + e + ")");
      BasicListUI.this.damageLayout();
    }
  }

  /**
   * A helper class which listens for {@link ListSelectionEvent}s
   * from the {@link JList}'s {@link ListSelectionModel}.
   */
  class ListSelectionHandler implements ListSelectionListener
  {
    /**
     * Called when the list selection changes.  
     *
     * @param e The event representing the change
     */
    public void valueChanged(ListSelectionEvent e)
    {
      //       System.err.println(this + ".valueChanged(" + e + ")");
    }
  }

  /**
   * A helper class which listens for {@link MouseEvent}s 
   * from the {@link JList}.
   */
  class MouseInputHandler implements MouseInputListener
  {
    /**
     * Called when a mouse button press/release cycle completes
     * on the {@link JList}
     *
     * @param event The event representing the mouse click
     */
    public void mouseClicked(MouseEvent event)
    {
    }

    /**
     * Called when a mouse button is pressed down on the
     * {@link JList}.
     *
     * @param event The event representing the mouse press
     */
    public void mousePressed(MouseEvent event)
    {
      // System.err.println("got mouse click event " + event);
      int row = BasicListUI.this.convertYToRow(event.getY());
      if (row == -1)
        return;

      // System.err.println("clicked on row " + row);
      BasicListUI.this.list.setSelectedIndex(row);
    }

    /**
     * Called when a mouse button is released on
     * the {@link JList}
     *
     * @param event The event representing the mouse press
     */
    public void mouseReleased(MouseEvent event)
    {
    }

    /**
     * Called when the mouse pointer enters the area bounded
     * by the {@link JList}
     *
     * @param event The event representing the mouse entry
     */
    public void mouseEntered(MouseEvent event)
    {
    }

    /**
     * Called when the mouse pointer leaves the area bounded
     * by the {@link JList}
     *
     * @param event The event representing the mouse exit
     */
    public void mouseExited(MouseEvent event)
    {
    }

    /**
     * Called when the mouse pointer moves over the area bounded
     * by the {@link JList} while a button is held down.
     *
     * @param event The event representing the mouse drag
     */
    public void mouseDragged(MouseEvent event)
    {
    }

    /**
     * Called when the mouse pointer moves over the area bounded
     * by the {@link JList}.
     *
     * @param event The event representing the mouse move
     */
    public void mouseMoved(MouseEvent event)
    {
    }
  }

  /**
   * Helper class which listens to {@link PropertyChangeEvent}s
   * from the {@link JList}.
   */
  class PropertyChangeHandler implements PropertyChangeListener
  {
    /**
     * Called when the {@link JList} changes one of its bound properties.
     *
     * @param e The event representing the property change
     */
    public void propertyChange(PropertyChangeEvent e)
    {
      // System.err.println(this + ".propertyChange(" + e + ")");
      if (e.getSource() == BasicListUI.this.list)
        {
          if (e.getOldValue() != null && e.getOldValue() instanceof ListModel)
            ((ListModel) e.getOldValue()).removeListDataListener(BasicListUI.this.listDataListener);

          if (e.getNewValue() != null && e.getNewValue() instanceof ListModel)
            ((ListModel) e.getNewValue()).addListDataListener(BasicListUI.this.listDataListener);
        }
      BasicListUI.this.damageLayout();
    }
  }

  /**
   * Creates a new BasicListUI for the component.
   *
   * @param c The component to create a UI for
   *
   * @return A new UI
   */
  public static ComponentUI createUI(final JComponent c)
  {
    return new BasicListUI();
  }

  /** The current focus listener. */
  FocusHandler focusListener;

  /** The data listener listening to the model. */
  ListDataHandler listDataListener;

  /** The selection listener listening to the selection model. */
  ListSelectionHandler listSelectionListener;

  /** The mouse listener listening to the list. */
  MouseInputHandler mouseInputListener;

  /** The property change listener listening to the list. */
  PropertyChangeHandler propertyChangeListener;

  /** Saved reference to the list this UI was created for. */
  JList list;

  /** The height of a single cell in the list. */
  int cellHeight;

  /** The width of a single cell in the list. */
  int cellWidth;

  /** 
   * An array of varying heights of cells in the list, in cases where each
   * cell might have a different height.
   */
  int[] cellHeights;

  /**
   * A simple counter. When nonzero, indicates that the UI class is out of
   * date with respect to the underlying list, and must recalculate the
   * list layout before painting or performing size calculations.
   */
  int updateLayoutStateNeeded;

  /**
   * Calculate the height of a particular row. If there is a fixed {@link
   * #cellHeight}, return it; otherwise return the specific row height
   * requested from the {@link #cellHeights} array. If the requested row
   * is invalid, return <code>-1</code>.
   *
   * @param row The row to get the height of
   *
   * @return The height, in pixels, of the specified row
   */
  int getRowHeight(int row)
  {
    if (row < 0 || row >= cellHeights.length)
      return -1;
    else if (cellHeight != -1)
      return cellHeight;
    else
      return cellHeights[row];
  }

  /**
   * Calculate the bounds of a particular cell, considering the upper left
   * corner of the list as the origin position <code>(0,0)</code>.
   *
   * @param l Ignored; calculates over <code>this.list</code>
   * @param index1 The first row to include in the bounds
   * @param index2 The last row to incude in the bounds
   *
   * @return A rectangle encompassing the range of rows between 
   * <code>index1</code> and <code>index2</code> inclusive
   */
  public Rectangle getCellBounds(JList l, int index1, int index2)
  {
    if (l != list || cellWidth == -1)
      return null;

    int lo = Math.min(index1, index2);
    int hi = Math.max(index1, index2);
    Rectangle lobounds = new Rectangle(0, convertRowToY(lo), cellWidth,
                                       getRowHeight(lo));
    Rectangle hibounds = new Rectangle(0, convertRowToY(hi), cellWidth,
                                       getRowHeight(hi));
    return lobounds.union(hibounds);
  }

  /**
   * Calculate the Y coordinate of the upper edge of a particular row,
   * considering the Y coordinate <code>0</code> to occur at the top of the
   * list.
   *
   * @param row The row to calculate the Y coordinate of
   *
   * @return The Y coordinate of the specified row, or <code>-1</code> if
   * the specified row number is invalid
   */
  int convertRowToY(int row)
  {
    int y = 0;
    for (int i = 0; i < row; ++i)
      {
        int h = getRowHeight(i);
        if (h == -1)
          return -1;
        y += h;
      }
    return y;
  }

  /**
   * Calculate the row number containing a particular Y coordinate,
   * considering the Y coodrinate <code>0</code> to occur at the top of the
   * list.
   *
   * @param y0 The Y coordinate to calculate the row number for
   *
   * @return The row number containing the specified Y value, or <code>-1</code>
   * if the specified Y coordinate is invalid
   */
  int convertYToRow(int y0)
  {
    for (int row = 0; row < cellHeights.length; ++row)
      {
        int h = getRowHeight(row);

        // System.err.println("convertYToRow(" + y0 + ") vs. " + h);
        if (y0 < h)
          return row;
        y0 -= h;
      }
    return -1;
  }

  /**
   * Recomputes the {@link #cellHeights}, {@link #cellHeight}, and {@link
   * #cellWidth} properties by examining the variouis properties of the
   * {@link JList}.
   */
  void updateLayoutState()
  {
    int nrows = list.getModel().getSize();
    cellHeight = -1;
    cellWidth = -1;
    if (cellHeights == null || cellHeights.length != nrows)
      cellHeights = new int[nrows];
    if (list.getFixedCellHeight() == -1 || list.getFixedCellWidth() == -1)
      {
        ListCellRenderer rend = list.getCellRenderer();
        for (int i = 0; i < nrows; ++i)
          {
            Component flyweight = rend.getListCellRendererComponent(list,
                                                                    list.getModel()
                                                                        .getElementAt(i),
                                                                    0, false,
                                                                    false);
            Dimension dim = flyweight.getPreferredSize();
            cellHeights[i] = dim.height;
            cellWidth = Math.max(cellWidth, dim.width);
          }
      }
    else
      {
        cellHeight = list.getFixedCellHeight();
        cellWidth = list.getFixedCellWidth();
      }
  }

  /**
   * Marks the current layout as damaged and requests revalidation from the
   * JList.
   *
   * @see #updateLayoutStateNeeded
   */
  void damageLayout()
  {
    updateLayoutStateNeeded = 1;
    list.revalidate();
  }

  /**
   * Calls {@link #updateLayoutState} if {@link #updateLayoutStateNeeded}
   * is nonzero, then resets {@link #updateLayoutStateNeeded} to zero.
   */
  void maybeUpdateLayoutState()
  {
    // System.err.println(this + ".maybeUpdateLayoutState()");
    if (updateLayoutStateNeeded != 0)
      {
        updateLayoutState();
        updateLayoutStateNeeded = 0;
      }
  }

  /**
   * Creates a new BasicListUI object.
   */
  public BasicListUI()
  {
    focusListener = new FocusHandler();
    listDataListener = new ListDataHandler();
    listSelectionListener = new ListSelectionHandler();
    mouseInputListener = new MouseInputHandler();
    propertyChangeListener = new PropertyChangeHandler();
    updateLayoutStateNeeded = 1;
  }

  /**
   * Installs various default settings (mostly colors) from the {@link
   * UIDefaults} into the {@link JList}
   *
   * @see #uninstallDefaults
   */
  void installDefaults()
  {
    UIDefaults defaults = UIManager.getLookAndFeelDefaults();
    list.setForeground(defaults.getColor("List.foreground"));
    list.setBackground(defaults.getColor("List.background"));
    list.setSelectionForeground(defaults.getColor("List.selectionForeground"));
    list.setSelectionBackground(defaults.getColor("List.selectionBackground"));
  }

  /**
   * Resets to <code>null</code> those defaults which were installed in 
   * {@link #installDefaults}
   */
  void uninstallDefaults()
  {
    UIDefaults defaults = UIManager.getLookAndFeelDefaults();
    list.setForeground(null);
    list.setBackground(null);
    list.setSelectionForeground(null);
    list.setSelectionBackground(null);
  }

  /**
   * Attaches all the listeners we have in the UI class to the {@link
   * JList}, its model and its selection model.
   *
   * @see #uninstallListeners
   */
  void installListeners()
  {
    list.addFocusListener(focusListener);
    list.getModel().addListDataListener(listDataListener);
    list.addListSelectionListener(listSelectionListener);
    list.addMouseListener(mouseInputListener);
    list.addMouseMotionListener(mouseInputListener);
    list.addPropertyChangeListener(propertyChangeListener);
  }

  /**
   * Detaches all the listeners we attached in {@link #installListeners}.
   */
  void uninstallListeners()
  {
    list.removeFocusListener(focusListener);
    list.getModel().removeListDataListener(listDataListener);
    list.removeListSelectionListener(listSelectionListener);
    list.removeMouseListener(mouseInputListener);
    list.removeMouseMotionListener(mouseInputListener);
    list.removePropertyChangeListener(propertyChangeListener);
  }

  /**
   * Installs keyboard actions for this UI in the {@link JList}.
   */
  void installKeyboardActions()
  {
  }

  /**
   * Uninstalls keyboard actions for this UI in the {@link JList}.
   */
  void uninstallKeyboardActions()
  {
  }

  /**
   * Installs the various aspects of the UI in the {@link JList}. In
   * particular, calls {@link #installDefaults}, {@link #installListeners}
   * and {@link #installKeyboardActions}. Also saves a reference to the
   * provided component, cast to a {@link JList}.
   *
   * @param c The {@link JList} to install the UI into
   */
  public void installUI(final JComponent c)
  {
    super.installUI(c);
    list = (JList) c;
    installDefaults();
    installListeners();
    installKeyboardActions();
    // System.err.println(this + ".installUI()");
    maybeUpdateLayoutState();
  }

  /**
   * Uninstalls all the aspects of the UI which were installed in {@link
   * #installUI}. When finished uninstalling, drops the saved reference to
   * the {@link JList}.
   *
   * @param c Ignored; the UI is uninstalled from the {@link JList}
   * reference saved during the call to {@link #installUI}
   */
  public void uninstallUI(final JComponent c)
  {
    uninstallKeyboardActions();
    uninstallListeners();
    uninstallDefaults();
    list = null;
  }

  /**
   * Gets the maximum size this list can assume.
   *
   * @param c The component to measure the size of
   *
   * @return A new Dimension representing the component's maximum size
   */
  public Dimension getMaximumSize(JComponent c)
  {
    return new Dimension(Integer.MAX_VALUE, Integer.MAX_VALUE);
  }

  /**
   * Gets the size this list would prefer to assume. This is calculated by
   * calling {@link #getCellBounds} over the entire list.
   *
   * @param c Ignored; uses the saved {@link JList} reference 
   *
   * @return DOCUMENT ME!
   */
  public Dimension getPreferredSize(JComponent c)
  {
    maybeUpdateLayoutState();
    if (list.getModel().getSize() == 0)
      return new Dimension(0, 0);
    Rectangle bounds = getCellBounds(list, 0, list.getModel().getSize() - 1);
    return bounds.getSize();
  }

  /**
   * Paints the packground of the list using the background color
   * of the specified component.
   *
   * @param g The graphics context to paint in
   * @param c The component to paint the background of
   */
  public void paintBackground(Graphics g, JComponent c)
  {
    Dimension size = getPreferredSize(c);
    Color save = g.getColor();
    g.setColor(c.getBackground());
    g.fillRect(0, 0, size.width, size.height);
    g.setColor(save);
  }

  /**
   * Paints a single cell in the list.
   *
   * @param g The graphics context to paint in
   * @param row The row number to paint
   * @param bounds The bounds of the cell to paint, assuming a coordinate
   * system beginning at <code>(0,0)</code> in the upper left corner of the
   * list
   * @param rend A cell renderer to paint with
   * @param data The data to provide to the cell renderer
   * @param sel A selection model to provide to the cell renderer
   * @param lead The lead selection index of the list
   */
  void paintCell(Graphics g, int row, Rectangle bounds, ListCellRenderer rend,
                 ListModel data, ListSelectionModel sel, int lead)
  {
    boolean is_sel = list.isSelectedIndex(row);
    boolean has_focus = false;
    Component comp = rend.getListCellRendererComponent(list,
                                                       data.getElementAt(row),
                                                       0, is_sel, has_focus);
    g.translate(bounds.x, bounds.y);
    comp.setBounds(new Rectangle(0, 0, bounds.width, bounds.height));
    comp.paint(g);
    g.translate(-bounds.x, -bounds.y);
  }

  /**
   * Paints the list by calling {@link #paintBackground} and then repeatedly
   * calling {@link #paintCell} for each visible cell in the list.
   *
   * @param g The graphics context to paint with
   * @param c Ignored; uses the saved {@link JList} reference 
   */
  public void paint(Graphics g, JComponent c)
  {
    int nrows = Math.min(list.getVisibleRowCount(), list.getModel().getSize());
    if (nrows == 0)
      return;

    maybeUpdateLayoutState();
    ListCellRenderer render = list.getCellRenderer();
    ListModel model = list.getModel();
    ListSelectionModel sel = list.getSelectionModel();
    int lead = sel.getLeadSelectionIndex();
    paintBackground(g, list);

    for (int row = 0; row < nrows; ++row)
      {
        Rectangle bounds = getCellBounds(list, row, row);
        paintCell(g, row, bounds, render, model, sel, lead);
      }
  }

  public int locationToIndex(JList list, Point location)
  {
    throw new Error("Not implemented");
  }

  public Point indexToLocation(JList list, int index)
  {
    throw new Error("Not implemented");
  }
}
