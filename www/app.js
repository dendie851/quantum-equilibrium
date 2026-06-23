// Default state variables - Load from localStorage if available to persist last known data
let wireAVal = parseFloat(localStorage.getItem('wire_a')) || 0;
let wireBVal = parseFloat(localStorage.getItem('wire_b')) || 0;
let selisihVal = parseFloat(localStorage.getItem('selisih')) || 0;
let threshold = parseFloat(localStorage.getItem('threshold')) || 200;
let isOverThreshold = (selisihVal > threshold);
let client = null;

// UI Elements
const statusBadge = document.getElementById('connection-status');
const warningLamp = document.getElementById('warning-lamp');
const lampLabel = document.getElementById('lamp-label');
const valA = document.getElementById('val-a');
const valB = document.getElementById('val-b');
const fillA = document.getElementById('fill-a');
const fillB = document.getElementById('fill-b');
const diffValue = document.getElementById('difference-value');
const diffBarFill = document.getElementById('difference-bar-fill');
const diffBarLabel = document.getElementById('difference-bar-label');
const thresholdDisplay = document.getElementById('threshold-display');
const cardA = document.getElementById('card-kawat-a');
const cardB = document.getElementById('card-kawat-b');
const hostInput = document.getElementById('mqtt-host');
const logBox = document.getElementById('log-box');
const debugContent = document.getElementById('debug-content');
const debugChevron = document.getElementById('debug-chevron');

// Set default host input
const defaultHost = window.location.protocol === 'https:' 
    ? `wss://${window.location.host}/mqtt` 
    : `ws://${window.location.host}/mqtt`;

// If page loaded locally without server, connect directly to localhost:9001
hostInput.value = window.location.host ? defaultHost : 'ws://localhost:9001';

// Toggle debug info panel
function toggleDebug() {
    debugContent.classList.toggle('show');
    debugChevron.textContent = debugContent.classList.contains('show') ? '▲' : '▼';
}

// Log utility
function log(message) {
    const timestamp = new Date().toLocaleTimeString();
    logBox.textContent = `[${timestamp}] ${message}\n` + logBox.textContent;
}

// Update Gauge UI Ring
// The gauge ring circumference is 2 * PI * r = 2 * 3.14 * 50 = 314
// Maximum ESP32 analog read is 4095
function updateGaugeRing(element, value, max = 4095) {
    const r = 50;
    const circumference = 2 * Math.PI * r;
    const percentage = Math.min(Math.max(value / max, 0), 1);
    const strokeDashoffset = circumference - (percentage * circumference);
    element.style.strokeDashoffset = strokeDashoffset;
}

// Update UI based on received state
function updateUI() {
    valA.textContent = wireAVal;
    valB.textContent = wireBVal;
    thresholdDisplay.textContent = threshold;

    updateGaugeRing(fillA, wireAVal);
    updateGaugeRing(fillB, wireBVal);

    // Update difference display
    diffValue.textContent = selisihVal;
    let percentOfThreshold = threshold > 0 ? Math.min((selisihVal / threshold) * 100, 100) : 0;
    diffBarFill.style.width = percentOfThreshold + '%';
    diffBarLabel.textContent = percentOfThreshold.toFixed(1) + '% of threshold';

    // Apply alert class to cards if threshold is exceeded
    if (isOverThreshold) {
        cardA.classList.add('alert');
        cardB.classList.add('alert');
        diffBarFill.style.background = 'linear-gradient(90deg, #f59e0b, #ef4444)';
        warningLamp.className = 'warning-lamp alert';
        lampLabel.className = 'lamp-label alert';
        lampLabel.textContent = 'ALARM ACTIVE (OVER THRESHOLD)';
    } else {
        cardA.classList.remove('alert');
        cardB.classList.remove('alert');
        diffBarFill.style.background = 'linear-gradient(90deg, #6366f1, #818cf8)';
        warningLamp.className = 'warning-lamp normal';
        lampLabel.className = 'lamp-label';
        lampLabel.textContent = 'SYSTEM NORMAL';
    }
}

// Initialize MQTT connection
function connectMQTT(brokerUrl) {
    log(`Connecting to MQTT broker: ${brokerUrl}...`);
    statusBadge.className = 'status-badge disconnected';
    statusBadge.textContent = 'Connecting...';

    if (client) {
        try {
            client.end();
        } catch (e) {
            console.error(e);
        }
    }

    try {
        client = mqtt.connect(brokerUrl, {
            connectTimeout: 5000,
            reconnectPeriod: 2000
        });

        client.on('connect', () => {
            log('Connected to MQTT broker successfully!');
            statusBadge.className = 'status-badge connected';
            statusBadge.textContent = 'Connected';

            // Subscribe to single topic
            client.subscribe('monitoring/wire', (err) => {
                if (!err) log('Subscribed to topic: monitoring/wire');
            });
        });

        client.on('message', (topic, message) => {
            const payloadStr = message.toString();
            log(`Msg received on [${topic}]: ${payloadStr}`);

            try {
                if (topic === 'monitoring/wire') {
                    const data = JSON.parse(payloadStr);
                    
                    // Support both English and Indonesian JSON keys for robustness
                    wireAVal = data.wire_a !== undefined ? data.wire_a : (data.kawat_a !== undefined ? data.kawat_a : wireAVal);
                    wireBVal = data.wire_b !== undefined ? data.wire_b : (data.kawat_b !== undefined ? data.kawat_b : wireBVal);
                    
                    // Parse selisih (difference) - primary key 'selisih', fallback to computed difference
                    selisihVal = data.selisih !== undefined ? data.selisih : Math.abs(wireAVal - wireBVal);
                    
                    threshold = data.threshold !== undefined ? data.threshold : (data.ambang_batas !== undefined ? data.ambang_batas : threshold);
                    
                    // Determine alarm: activate when selisih (difference) exceeds threshold
                    isOverThreshold = (selisihVal > threshold);

                    // Save to localStorage to persist last known data
                    localStorage.setItem('wire_a', wireAVal);
                    localStorage.setItem('wire_b', wireBVal);
                    localStorage.setItem('selisih', selisihVal);
                    localStorage.setItem('threshold', threshold);
                    localStorage.setItem('isOverThreshold', isOverThreshold);

                    updateUI();
                }
            } catch (err) {
                log(`Error parsing JSON: ${err.message}`);
            }
        });

        client.on('close', () => {
            log('MQTT connection closed.');
            statusBadge.className = 'status-badge disconnected';
            statusBadge.textContent = 'Disconnected';
        });

        client.on('error', (err) => {
            log(`MQTT connection error: ${err.message}`);
            statusBadge.className = 'status-badge disconnected';
            statusBadge.textContent = 'Error';
        });

    } catch (err) {
        log(`Failed to initiate MQTT client: ${err.message}`);
    }
}

// Reconnect handler triggered by UI button
function reconnectMQTT() {
    const url = hostInput.value.trim();
    if (url) {
        connectMQTT(url);
    } else {
        log('Error: MQTT Host URL cannot be empty.');
    }
}

// Initial Setup
updateUI();
// Auto connect on startup
connectMQTT(hostInput.value);
