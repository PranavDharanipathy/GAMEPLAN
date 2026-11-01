package org.gameplan.vsdk;

import java.util.Map;

/** Thin access to the bridge JSON parser from shadow code. */
final class MiniJson {
    private MiniJson() {}
    @SuppressWarnings("unchecked")
    static Map<String, Object> obj(String s) {
        try {
            Class<?> j = Class.forName("org.gameplan.runtime.Json");
            return (Map<String, Object>) j.getMethod("parse", String.class).invoke(null, s);
        } catch (Exception e) { throw new RuntimeException(e); }
    }
    @SuppressWarnings("unchecked") static Map<String, Object> asMap(Object o) { return (Map<String, Object>) o; }
    static double num(Object o) { return o instanceof Number ? ((Number) o).doubleValue() : 0.0; }
}
