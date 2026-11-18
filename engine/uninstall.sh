#!/bin/bash
# widi-key yerel motorunu kaldırır (modeller Hugging Face önbelleğinde kalır: ~/.cache/huggingface)
LABEL="com.wabiblack.widikey.engine"
launchctl bootout "gui/$(id -u)/$LABEL" >/dev/null 2>&1 || true
rm -f "$HOME/Library/LaunchAgents/$LABEL.plist"
rm -rf "$HOME/Library/Application Support/widi-key/engine"
echo "widi-key motoru kaldırıldı."
