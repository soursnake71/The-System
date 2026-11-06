package app.callbuddy;

import android.content.Context;
import android.content.SharedPreferences;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

/** Settings shared by the app screen and the call screening service. */
final class Prefs {
  static final String URL = "server_url", NUMBERS = "caller_numbers", MODE = "mode",
      AUTO = "auto_decline", TWILIO = "twilio_number";

  static SharedPreferences get(Context c) {
    return c.getSharedPreferences("callbuddy", Context.MODE_PRIVATE);
  }

  /** Same rule as the server: digits only, drop a leading US "1" from 11-digit numbers. */
  static String digits(String s) {
    String d = s == null ? "" : s.replaceAll("\\D", "");
    return d.length() == 11 && d.startsWith("1") ? d.substring(1) : d;
  }

  static Set<String> numbers(SharedPreferences p) {
    Set<String> out = new HashSet<>();
    for (String n : Arrays.asList(p.getString(NUMBERS, "").split(","))) {
      String d = digits(n);
      if (!d.isEmpty()) out.add(d);
    }
    return out;
  }

  private Prefs() {}
}
