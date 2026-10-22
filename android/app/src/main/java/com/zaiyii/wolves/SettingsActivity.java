package com.zaiyii.wolves;

import android.os.Bundle;
import android.preference.PreferenceActivity;

/** 动态壁纸的设置页：场景、色调、播放速度、帧率。选项保存在默认 SharedPreferences，壁纸服务会立即读取。 */
@SuppressWarnings("deprecation")
public class SettingsActivity extends PreferenceActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        addPreferencesFromResource(R.xml.preferences);
    }
}
