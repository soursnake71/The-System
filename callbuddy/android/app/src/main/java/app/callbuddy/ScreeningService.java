package app.callbuddy;

import android.content.SharedPreferences;
import android.net.Uri;
import android.telecom.Call;
import android.telecom.CallScreeningService;

/**
 * Android calls this for every incoming call (once Call Buddy is the call screening app).
 * If the call is from the person and the AI is on, it declines the call. The carrier's
 * "forward when busy" then sends it to the Twilio number, where the AI answers in your voice.
 *
 * It does NOT decline a second call from them within 20 minutes: that is either the AI's
 * emergency transfer to you, or them calling right back, and either way you should hear it ring.
 */
public class ScreeningService extends CallScreeningService {
  static final long LET_THROUGH_MS = 20 * 60 * 1000;

  @Override
  public void onScreenCall(Call.Details details) {
    CallResponse.Builder r = new CallResponse.Builder();
    if (details.getCallDirection() == Call.Details.DIRECTION_INCOMING && shouldSendToAi(details.getHandle())) {
      r.setDisallowCall(true).setRejectCall(true).setSkipCallLog(false).setSkipNotification(true);
    }
    respondToCall(details, r.build());
  }

  private boolean shouldSendToAi(Uri handle) {
    SharedPreferences p = Prefs.get(this);
    if (!p.getBoolean(Prefs.AUTO, false) || !"ai".equals(p.getString(Prefs.MODE, "ai"))) return false;
    String num = Prefs.digits(handle == null ? "" : handle.getSchemeSpecificPart());
    if (num.isEmpty() || !Prefs.numbers(p).contains(num)) return false;
    long now = System.currentTimeMillis();
    String key = "last_" + num;
    if (now - p.getLong(key, 0) < LET_THROUGH_MS) return false;
    p.edit().putLong(key, now).apply();
    return true;
  }
}
