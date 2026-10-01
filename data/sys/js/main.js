document.addEventListener("DOMContentLoaded", () => {
    console.log("EPY Portal: Main Loaded");
    setupNavigation();
    setupGlobalActions();
    fetchSystemStats();
});

// --- Fetch System Stats ---
function fetchSystemStats() {
    fetch('/dynamic/system-stats.json?' + Math.random().toString(36).substring(2, 10))
        .then(res => res.json())
        .then(data => {
            const statsMap = {
                'free-storage': '.free-storage',
                'used-storage': '.used-storage',
                'total-capacity': '.total-capacity',
                'temperature': '.temperature',
                'humidity': '.humidity',
                'battery-level': '.battery-level',
                'firmware': '.firmware'
            };

            Object.keys(statsMap).forEach(jsonKey => {
                if (data[jsonKey]) {
                    const element = document.querySelector(statsMap[jsonKey]);
                    if (element) {
                        element.innerText = data[jsonKey];
                    }
                }
            });

            if (data['is-charging'] === "true") {
                const icon = document.querySelector('.charging-icon');
                if (icon) icon.classList.remove('not-charging');
            } else {
                const icon = document.querySelector('.charging-icon');
                if (icon) icon.classList.add('not-charging');
            }
        })
        .catch(err => console.error("Failed to fetch system stats:", err));
}

// --- Custom Confirmation Modal ---
window.showConfirm = function (message, onConfirmCallback) {
    let overlay = document.getElementById('confirm-overlay');
    if (!overlay) {
        overlay = document.createElement('div');
        overlay.id = 'confirm-overlay';
        overlay.className = 'overlay hidden';
        overlay.innerHTML = `
            <div class="modal">
                <h3>Confirm Action</h3>
                <p id="confirm-msg"></p>
                <div class="modal-actions">
                    <button id="btn-modal-cancel" class="btn">Cancel</button>
                    <button id="btn-modal-confirm" class="btn btn-danger">Confirm</button>
                </div>
            </div>`;
        document.body.appendChild(overlay);
    }

    const msgEl = document.getElementById('confirm-msg');
    const btnCancel = document.getElementById('btn-modal-cancel');
    const btnConfirm = document.getElementById('btn-modal-confirm');

    msgEl.innerText = message;
    overlay.classList.remove('hidden');

    const newBtnConfirm = btnConfirm.cloneNode(true);
    btnConfirm.parentNode.replaceChild(newBtnConfirm, btnConfirm);

    const newBtnCancel = btnCancel.cloneNode(true);
    btnCancel.parentNode.replaceChild(newBtnCancel, btnCancel);

    newBtnConfirm.addEventListener('click', () => {
        overlay.classList.add('hidden');
        if (onConfirmCallback) onConfirmCallback();
    });

    newBtnCancel.addEventListener('click', () => {
        overlay.classList.add('hidden');
    });
};

function setupNavigation() {
    const navBtns = document.querySelectorAll('.nav-btn[data-target]');
    navBtns.forEach(btn => {
        btn.addEventListener('click', () => {
            navBtns.forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));

            btn.classList.add('active');
            const targetId = btn.dataset.target;
            const targetView = document.getElementById(targetId);
            if (targetView) {
                targetView.classList.add('active');

                if (targetId === 'filemanager' && typeof fetchFiles === 'function') {
                    fetchFiles();
                }
                if (targetId === 'tools' && typeof loadData === 'function') {
                    loadData();
                }
            }
        });
    });
}

function setupGlobalActions() {
    // 1. Version Overlay
    const versionBtn = document.getElementById('btn-version');
    if (versionBtn) {
        versionBtn.addEventListener('click', () => {
            const verOverlay = document.getElementById('version-overlay');
            if (verOverlay) verOverlay.classList.remove('hidden');
        });
    }

    // 2. Reboot Action (Now inside Version Overlay)
    const overlayRebootBtn = document.getElementById('btn-overlay-reboot');
    if (overlayRebootBtn) {
        overlayRebootBtn.addEventListener('click', () => {
            // Close the version overlay first
            document.getElementById('version-overlay').classList.add('hidden');

            // Show confirmation
            window.showConfirm("Reboot device? This will take about 10 seconds.", () => {
                fetch('/system/reboot');
                document.body.innerHTML = "<div style='display:flex;justify-content:center;align-items:center;height:100vh;flex-direction:column;'><h2>Rebooting...</h2><p>Please wait.</p></div>";
                setTimeout(() => window.location.reload(), 10000);
            });
        });
    }

    // 3. Settings Modal Handlers
    const settingsBtn = document.getElementById('btn-settings');
    if (settingsBtn) settingsBtn.addEventListener('click', openSettingsOverlay);

    const settingsCancelBtn = document.getElementById('btn-settings-cancel');
    if (settingsCancelBtn) {
        settingsCancelBtn.addEventListener('click', () => {
            document.getElementById('settings-overlay').classList.add('hidden');
        });
    }

    const settingsSaveBtn = document.getElementById('btn-settings-save');
    if (settingsSaveBtn) settingsSaveBtn.addEventListener('click', saveSettings);

    // 4. Eco Mode Toggle -> Interval Disable Logic
    const settingEcoCheckbox = document.getElementById('setting-eco');
    const settingIntervalSelect = document.getElementById('setting-interval');
    // if (settingEcoCheckbox && settingIntervalSelect) {
    //     settingEcoCheckbox.addEventListener('change', (e) => {
    //         // Disable interval dropdown when Eco mode is unchecked (Constant mode)
    //         settingIntervalSelect.disabled = !e.target.checked;
    //     });
    // }
}

// --- Settings Logic ---
function openSettingsOverlay() {
    const overlay = document.getElementById('settings-overlay');
    if (overlay) overlay.classList.remove('hidden');

    const randId = Math.random().toString(36).substring(2);
    Promise.all([
        fetch('/dynamic/setting.json?' + randId).then(res => res.json()),
        fetch('/data/system.json?' + randId).then(res => res.json())
    ])
        .then(([settingsData, systemData]) => {
            // Populate "screen" dropdown from system.json -> epd_screens array
            const screenSelect = document.getElementById('setting-screen');
            screenSelect.innerHTML = '';
            if (systemData.epd_screens && Array.isArray(systemData.epd_screens)) {
                systemData.epd_screens.forEach(screenName => {
                    const opt = document.createElement('option');
                    opt.value = screenName;
                    opt.innerText = screenName;
                    screenSelect.appendChild(opt);
                });
            }

            // Bind data from setting.json
            const isEco = (settingsData.isEcoMode === true);
            document.getElementById('setting-eco').checked = isEco;

            const isBatteryAttached = (settingsData.isBatteryAttached === true);
            document.getElementById('setting-battery').checked = isBatteryAttached;

            // Disable interval select if Eco Mode is initially false
            const intervalSelect = document.getElementById('setting-interval');
            // intervalSelect.disabled = !isEco;

            if (Array.from(intervalSelect.options).some(opt => opt.value == settingsData.interval)) {
                intervalSelect.value = settingsData.interval;
            }

            const ecoThreshold = (settingsData.ecoThresholdPct != null) ? settingsData.ecoThresholdPct : 0;
            document.getElementById('setting-eco-threshold').value = ecoThreshold;

            if (settingsData.screen) {
                screenSelect.value = settingsData.screen;
            }
        })
        .catch(err => {
            console.error("Failed to load settings configuration:", err);
        });
}

function saveSettings() {
    const btnSave = document.getElementById('btn-settings-save');
    const originalText = btnSave.innerText;
    btnSave.innerText = "Saving...";
    btnSave.disabled = true;

    // Build payload mapping
    // Note: The `.value` property of disabled elements is still readable by JS
    const payload = {
        isEcoMode: document.getElementById('setting-eco').checked,
        isBatteryAttached: document.getElementById('setting-battery').checked,
        interval: parseInt(document.getElementById('setting-interval').value, 10),
        ecoThresholdPct: parseInt(document.getElementById('setting-eco-threshold').value, 10) || 0,
        screen: document.getElementById('setting-screen').value
    };

    fetch('/dynamic/setting.json', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
    })
        .then(res => {
            if (res.ok) {
                document.getElementById('settings-overlay').classList.add('hidden');
            } else {
                alert("Failed to save settings.");
            }
        })
        .catch(err => {
            console.error("Network error:", err);
            alert("Network error. Could not save settings.");
        })
        .finally(() => {
            btnSave.innerText = originalText;
            btnSave.disabled = false;
        });
}