package com.android.bluetooth;

import android.content.ContentResolver;
import android.content.Context;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.drawable.Drawable;
import android.provider.Settings;
import android.util.Log;
import android.widget.ImageView;
import androidx.core.graphics.drawable.DrawableCompat;

import com.android.bluetooth.R;
import com.asus.commonres.AsusResTheme;
import com.asus.commonres.AsusResUtils;
import com.android.bluetooth.Utils;

public class ThemeUtils {
    private static final String TAG = "ThemeUtils";
    private static final int VALUE_THEME_MODE_LIGHT = 0;
    private static final int VALUE_THEME_MODE_DARK = 1;
    private static final int VALUE_THEME_MODE_DOWNLOAD = 2;

    private static int IS_THEME_CHANGED = 0;
    private static int THEME_MAIN_COLOR = 0;
    private static int THEME_HIGHLIGHT_COLOR = 0;
    private static int THEME_TEXT_COLOR = 0;
    private static int THEME_BACKGROUND_COLOR = 0;
    private static int IS_LIGHT_THEME = 0;
    private static int SYSTEM_THEME = 0;

    public static final String SYSTEM_THEME_MODE = "system_theme_mode";
    public static final String IS_CLASSIC_ZENUI = "is_classic_zenui";
    private static final String KEY_THEME_MAIN_COLOR = "theme_main_color";
    private static final String KEY_IS_THEME_CHANGED = "is_theme_changed";
    private static final String KEY_THEME_HIGHLIGHT_COLOR = "theme_highlight_color";
    private static final String KEY_THEME_TEXT_COLOR = "theme_text_color";
    private static final String KEY_THEME_BACKGROUND_COLOR = "theme_background_color";
    private static final String KEY_IS_LIGHT_THEME = "is_light_theme";

    private static boolean IS_SYSTEM_THEME_CHANGE = false;

    public static void loadThemeColorCodeFromProvider(Context context) {
        final ContentResolver cr = context.getContentResolver();

        IS_THEME_CHANGED = Settings.Global.getInt(cr, KEY_IS_THEME_CHANGED, 0);
        SYSTEM_THEME = Settings.Global.getInt(cr, SYSTEM_THEME_MODE, VALUE_THEME_MODE_LIGHT);

        if (SYSTEM_THEME == 0 || SYSTEM_THEME == 1) {
            IS_THEME_CHANGED = 0;
        }

        THEME_MAIN_COLOR = Settings.Global.getInt(cr, KEY_THEME_MAIN_COLOR, 0);
        THEME_HIGHLIGHT_COLOR = Settings.Global.getInt(cr, KEY_THEME_HIGHLIGHT_COLOR, 0);
        THEME_TEXT_COLOR = Settings.Global.getInt(cr, KEY_THEME_TEXT_COLOR, 0);
        THEME_BACKGROUND_COLOR = Settings.Global.getInt(cr, KEY_THEME_BACKGROUND_COLOR, 0);
        IS_LIGHT_THEME = Settings.Global.getInt(cr, KEY_IS_LIGHT_THEME, 0);

        Log.d(TAG, "Global Theme Loaded");
    }

    public static boolean isDownloadTheme() {
        return SYSTEM_THEME == VALUE_THEME_MODE_DOWNLOAD && !isFollowSystemTheme();
    }

    public static boolean isFollowSystemTheme() {
        return ((THEME_MAIN_COLOR == 0)
                && (THEME_HIGHLIGHT_COLOR == 0)
                && (THEME_TEXT_COLOR == 0)
                && (THEME_BACKGROUND_COLOR == 0));
    }

    public static boolean isRogTheme(Context context) {
        // Follow Common UI api
        return AsusResUtils.shouldApplyRogTheme(context, isLightStatusBar(context));
    }

    public static boolean isClassicZenUITheme(Context context) {
        if(Utils.isPicassoFone()) {
            return android.provider.Settings.System.getInt(
                    context.getContentResolver(), "system_theme_type", 1) == 1;
        } else {
            return android.provider.Settings.System.getInt(
                    context.getContentResolver(), "system_theme_type", 0) == 1;
        }
    }

    public static int getThemeColorCode(Context context) {
        if (isDownloadTheme()) {
            return THEME_MAIN_COLOR;
        } else {
            return AsusResTheme.getAttributeColorRes(context,
                    AsusResUtils.getAsusResThemeStyle(context), android.R.attr.colorAccent);
        }
    }

    public static int getHighLightColorCode(Context context) {
        if (isDownloadTheme()) {
            return THEME_HIGHLIGHT_COLOR;
        } else {
            return isRogTheme(context)
                    ? context.getResources().getColor(R.color.settings_rog_highlight_color)
                    : context.getResources().getColor(R.color.settings_zenUI_highlight_color);
        }
    }

    public static int getTextColorCode(Context context) {
        if (isDownloadTheme()) {
            return THEME_TEXT_COLOR;
        } else {
            return AsusResTheme.getAttributeColorRes(context,
                    AsusResUtils.getAsusResThemeStyle(context), android.R.attr.textColorPrimary);
        }
    }

    public static int getBgColorCode(Context context) {
        if (isDownloadTheme()) {
            return THEME_BACKGROUND_COLOR;
        } else {
            return AsusResTheme.getAttributeColorRes(context,
                    AsusResUtils.getAsusResThemeStyle(context), android.R.attr.windowBackground);
        }
    }

    public static boolean isLightStatusBar(Context context) {
        if (SYSTEM_THEME == VALUE_THEME_MODE_DARK
                || SYSTEM_THEME == VALUE_THEME_MODE_LIGHT){
            return isLightTheme(context);
        }
        return (IS_LIGHT_THEME == 1);
    }

    public static boolean isLightTheme(Context context) {
        int currentNightMode = context.getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK;
        return currentNightMode != Configuration.UI_MODE_NIGHT_YES;
    }

    public static void tintImageView(ImageView imageview, int tintColor){
        if(imageview != null && imageview.getDrawable() != null){
            imageview.setImageTintList(ColorStateList.valueOf(tintColor));
        }
    }

    public static Drawable tintDrawable(Drawable drawable, int colorCode) {
        final Drawable wrappedDrawable = DrawableCompat.wrap(drawable);
        DrawableCompat.setTintList(wrappedDrawable, ColorStateList.valueOf(colorCode));
        return wrappedDrawable;
    }
}