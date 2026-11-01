package org.opencv.core;

/** Stand-in for OpenCV's pure-Java Point (used in AprilTag detection results); used only when OpenCV is absent. */
public class Point {
    public double x, y;
    public Point() {}
    public Point(double x, double y) { this.x = x; this.y = y; }
    public Point clone() { return new Point(x, y); }
    @Override public String toString() { return "{" + x + ", " + y + "}"; }
}
