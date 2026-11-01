package org.gameplan.vsdk;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

import org.firstinspires.ftc.robotcore.external.Func;
import org.firstinspires.ftc.robotcore.external.Telemetry;
import org.gameplan.runtime.api.Sim;

/**
 * Driver Station telemetry. update() costs CPU/comm time and is transmitted at most every
 * msTransmissionInterval (250 ms default) of virtual time, as the SDK rate-limits it.
 */
public class TelemetryImpl implements Telemetry {
    private final List<Object> entries = new ArrayList<>();   // ItemImpl or LineImpl
    private final List<String> logLines = new ArrayList<>();
    private boolean autoClear = true;
    private int interval = 250;
    private String itemSep = " | ", capSep = " : ";
    private final LogImpl log = new LogImpl();

    final class ItemImpl implements Item {
        String caption;
        String value = "";
        Func<?> func;
        String fmt;
        boolean retained;
        ItemImpl(String c) { caption = c; }
        String render() {
            if (func != null) { Object v = func.value(); return fmt != null ? String.format(Locale.US, fmt, v) : String.valueOf(v); }
            return value;
        }
        @Override public String getCaption() { return caption; }
        @Override public Item setCaption(String c) { caption = c; return this; }
        @Override public Item setValue(String f, Object... args) { value = String.format(Locale.US, f, args); func = null; return this; }
        @Override public Item setValue(Object v) { value = String.valueOf(v); func = null; return this; }
        @Override public <T> Item setValue(Func<T> f) { func = f; fmt = null; return this; }
        @Override public <T> Item setValue(String f, Func<T> fn) { func = fn; fmt = f; return this; }
        @Override public Item setRetained(Boolean r) { retained = r != null && r; return this; }
        @Override public boolean isRetained() { return retained; }
        @Override public Item addData(String c, String f, Object... a) { return TelemetryImpl.this.addData(c, f, a); }
        @Override public Item addData(String c, Object v) { return TelemetryImpl.this.addData(c, v); }
        @Override public <T> Item addData(String c, Func<T> f) { return TelemetryImpl.this.addData(c, f); }
        @Override public <T> Item addData(String c, String f, Func<T> fn) { return TelemetryImpl.this.addData(c, f, fn); }
    }

    final class LineImpl implements Line {
        final String caption;
        final List<ItemImpl> items = new ArrayList<>();
        LineImpl(String c) { caption = c; }
        @Override public Item addData(String c, String f, Object... a) { ItemImpl i = new ItemImpl(c); i.setValue(f, a); items.add(i); return i; }
        @Override public Item addData(String c, Object v) { ItemImpl i = new ItemImpl(c); i.setValue(v); items.add(i); return i; }
        @Override public <T> Item addData(String c, Func<T> f) { ItemImpl i = new ItemImpl(c); i.setValue(f); items.add(i); return i; }
        @Override public <T> Item addData(String c, String f, Func<T> fn) { ItemImpl i = new ItemImpl(c); i.setValue(f, fn); items.add(i); return i; }
    }

    final class LogImpl implements Log {
        int capacity = 9;
        DisplayOrder order = DisplayOrder.OLDEST_FIRST;
        @Override public int getCapacity() { return capacity; }
        @Override public void setCapacity(int c) { capacity = c; }
        @Override public DisplayOrder getDisplayOrder() { return order; }
        @Override public void setDisplayOrder(DisplayOrder o) { order = o; }
        @Override public void add(String e) { logLines.add(e); while (logLines.size() > capacity) logLines.remove(0); }
        @Override public void add(String f, Object... a) { add(String.format(Locale.US, f, a)); }
        @Override public void clear() { logLines.clear(); }
    }

    @Override public synchronized Item addData(String c, String f, Object... a) { ItemImpl i = new ItemImpl(c); i.setValue(f, a); entries.add(i); return i; }
    @Override public synchronized Item addData(String c, Object v) { ItemImpl i = new ItemImpl(c); i.setValue(v); entries.add(i); return i; }
    @Override public synchronized <T> Item addData(String c, Func<T> f) { ItemImpl i = new ItemImpl(c); i.setValue(f); entries.add(i); return i; }
    @Override public synchronized <T> Item addData(String c, String f, Func<T> fn) { ItemImpl i = new ItemImpl(c); i.setValue(f, fn); entries.add(i); return i; }
    @Override public synchronized boolean removeItem(Item item) { return entries.remove(item); }
    @Override public synchronized void clear() { entries.removeIf(e -> !(e instanceof ItemImpl && ((ItemImpl) e).retained)); }
    @Override public synchronized void clearAll() { entries.clear(); logLines.clear(); }
    @Override public Object addAction(Runnable r) { return r; }
    @Override public boolean removeAction(Object t) { return true; }
    @Override public void speak(String t) { Sim.log("INFO", "telemetry.speak: " + t); }
    @Override public void speak(String t, String l, String c) { speak(t); }
    @Override public synchronized boolean update() {
        List<String> lines = new ArrayList<>();
        for (Object e : entries) {
            if (e instanceof ItemImpl) { ItemImpl i = (ItemImpl) e; lines.add(i.caption + capSep + i.render()); }
            else {
                LineImpl l = (LineImpl) e;
                StringBuilder b = new StringBuilder(l.caption);
                for (int k = 0; k < l.items.size(); k++) { if (k > 0 || l.caption.length() > 0) b.append(itemSep); b.append(l.items.get(k).caption).append(capSep).append(l.items.get(k).render()); }
                lines.add(b.toString());
            }
        }
        lines.addAll(logLines);
        Sim.telemetry(lines, interval);
        if (autoClear) clear();
        return true;
    }
    @Override public synchronized Line addLine() { LineImpl l = new LineImpl(""); entries.add(l); return l; }
    @Override public synchronized Line addLine(String c) { LineImpl l = new LineImpl(c); entries.add(l); return l; }
    @Override public synchronized boolean removeLine(Line l) { return entries.remove(l); }
    @Override public boolean isAutoClear() { return autoClear; }
    @Override public void setAutoClear(boolean a) { autoClear = a; }
    @Override public int getMsTransmissionInterval() { return interval; }
    @Override public void setMsTransmissionInterval(int ms) { interval = ms; }
    @Override public String getItemSeparator() { return itemSep; }
    @Override public void setItemSeparator(String s) { itemSep = s; }
    @Override public String getCaptionValueSeparator() { return capSep; }
    @Override public void setCaptionValueSeparator(String s) { capSep = s; }
    @Override public void setDisplayFormat(DisplayFormat f) {}
    @Override public Log log() { return log; }
}
