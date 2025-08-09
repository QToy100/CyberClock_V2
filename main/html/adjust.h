#ifndef _ADJUST_HTML_
#define _ADJUST_HTML_

const char adjust_html[] = R"rawliteral(


<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title id="title-page">7-Segment Adjuster</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            margin: 20px;
            background-color: #f5f5f5;
        }
        h1 {
            color: #333;
            margin-bottom: 30px;
        }
        .digits-container {
            display: flex;
            justify-content: center;
            gap: 40px;
            margin-bottom: 30px;
        }
        .digit-container {
            position: relative;
            width: 200px;
            height: 360px;
            background-color: #222;
            border-radius: 20px;
            padding: 30px;
            box-shadow: 0 4px 8px rgba(0,0,0,0.2);
        }
        .segment {
            position: absolute;
            background-color: #444;
            cursor: pointer;
            display: flex;
            justify-content: center;
            align-items: center;
            color: white;
            font-weight: bold;
            border-radius: 6px;
            transition: background-color 0.2s;
            font-size: 18px;
        }
        .segment:hover {
            background-color: #555;
        }
        .segment.selected {
            background-color: #4CAF50;
        }
        .segment-value {
            position: absolute;
            bottom: -25px;
            width: 100%;
            text-align: center;
            font-size: 14px;
            color: white;
        }
        /* Horizontal segments */
        .segment-h {
            height: 24px;
        }
        /* Vertical segments */
        .segment-v {
            width: 24px;
        }
        /* Adjusted segment positions - centered and optimized spacing */
        .segment-a { top: 80px; left: 80px; width: 100px; } /* top - centered */
        .segment-b { top: 110px; left: 180px; height: 90px; } /* top right */
        .segment-c { top: 230px; left: 180px; height: 90px; } /* bottom right */
        .segment-d { top: 325px; left: 80px; width: 100px; } /* bottom - raised up */
        .segment-e { top: 230px; left: 60px; height: 90px; } /* bottom left */
        .segment-f { top: 110px; left: 60px; height: 90px; } /* top left */
        .segment-g { top: 205px; left: 80px; width: 100px; } /* middle - raised up */
        
        .instructions {
            margin-top: 40px;
            padding: 20px;
            background-color: #eee;
            border-radius: 10px;
            max-width: 800px;
            margin-left: auto;
            margin-right: auto;
            font-size: 16px;
        }
        .instructions h3 {
            margin-top: 0;
        }
    </style>
</head>
<body>
    <h1 id="title-main">7-Segment Display Adjuster</h1>
    
    <div class="digits-container">
        <!-- Four digits -->
        <div class="digit-container" id="digit-1">
            <div class="segment segment-h segment-a" data-offset="0">0</div>
            <div class="segment segment-v segment-b" data-offset="0">0</div>
            <div class="segment segment-v segment-c" data-offset="0">0</div>
            <div class="segment segment-h segment-d" data-offset="0">0</div>
            <div class="segment segment-v segment-e" data-offset="0">0</div>
            <div class="segment segment-v segment-f" data-offset="0">0</div>
            <div class="segment segment-h segment-g" data-offset="0">0</div>
            <div class="segment-value" id="digit-label-1">Digit 1</div>
        </div>
        
        <div class="digit-container" id="digit-2">
            <div class="segment segment-h segment-a" data-offset="0">0</div>
            <div class="segment segment-v segment-b" data-offset="0">0</div>
            <div class="segment segment-v segment-c" data-offset="0">0</div>
            <div class="segment segment-h segment-d" data-offset="0">0</div>
            <div class="segment segment-v segment-e" data-offset="0">0</div>
            <div class="segment segment-v segment-f" data-offset="0">0</div>
            <div class="segment segment-h segment-g" data-offset="0">0</div>
            <div class="segment-value" id="digit-label-2">Digit 2</div>
        </div>
        
        <div class="digit-container" id="digit-3">
            <div class="segment segment-h segment-a" data-offset="0">0</div>
            <div class="segment segment-v segment-b" data-offset="0">0</div>
            <div class="segment segment-v segment-c" data-offset="0">0</div>
            <div class="segment segment-h segment-d" data-offset="0">0</div>
            <div class="segment segment-v segment-e" data-offset="0">0</div>
            <div class="segment segment-v segment-f" data-offset="0">0</div>
            <div class="segment segment-h segment-g" data-offset="0">0</div>
            <div class="segment-value" id="digit-label-3">Digit 3</div>
        </div>
        
        <div class="digit-container" id="digit-4">
            <div class="segment segment-h segment-a" data-offset="0">0</div>
            <div class="segment segment-v segment-b" data-offset="0">0</div>
            <div class="segment segment-v segment-c" data-offset="0">0</div>
            <div class="segment segment-h segment-d" data-offset="0">0</div>
            <div class="segment segment-v segment-e" data-offset="0">0</div>
            <div class="segment segment-v segment-f" data-offset="0">0</div>
            <div class="segment segment-h segment-g" data-offset="0">0</div>
            <div class="segment-value" id="digit-label-4">Digit 4</div>
        </div>
    </div>
    
    <div class="instructions" id="instructions">
        <h3 id="instructions-title">Instructions</h3>
        <p id="instructions-p1">Click on a segment to select it (turns green). Then use:</p>
        <ul style="list-style-type: none; padding: 0;">
            <li id="instructions-li1">↑/↓ Arrow keys: Adjust by ±1</li>
            <li id="instructions-li2">←/→ Arrow keys: Adjust by ±5</li>
        </ul>
        <p id="instructions-p2">The offset value is displayed on each segment.</p>
    </div>

<script>
    // 多语言资源
    const LANG_RES = {
        en: {
            PAGE_TITLE: "7-Segment Adjuster",
            MAIN_TITLE: "7-Segment Display Adjuster",
            DIGIT: "Digit",
            INSTRUCTIONS: "Instructions",
            CLICK_SEGMENT: "Click on a segment to select it (turns green). Then use:",
            ARROW_1: "↑/↓ Arrow keys: Adjust by ±1",
            ARROW_2: "←/→ Arrow keys: Adjust by ±5",
            OFFSET_HINT: "The offset value is displayed on each segment, and it is not recommended to exceed 30."
        },
        zh: {
            PAGE_TITLE: "段码位置校准",
            MAIN_TITLE: "段码位置校准工具",
            DIGIT: "段码",
            INSTRUCTIONS: "操作说明",
            CLICK_SEGMENT: "点击某个段码选中（变绿色），然后使用：",
            ARROW_1: "↑/↓ 方向键：每次调整±1",
            ARROW_2: "←/→ 方向键：每次调整±5",
            OFFSET_HINT: "每个段码上显示当前偏移值,偏移值不建议超过30。"
        }
    };
    function getLang() {
        const lang = navigator.language || navigator.userLanguage || '';
        return lang.startsWith('zh') ? 'zh' : 'en';
    }
    const RES = LANG_RES[getLang()];

    // 设置多语言文本
    window.addEventListener('DOMContentLoaded', function() {
        document.title = RES.PAGE_TITLE;
        document.getElementById('title-page').textContent = RES.PAGE_TITLE;
        document.getElementById('title-main').textContent = RES.MAIN_TITLE;
        for (let i = 1; i <= 4; i++) {
            document.getElementById('digit-label-' + i).textContent = RES.DIGIT + ' ' + i;
        }
        document.getElementById('instructions-title').textContent = RES.INSTRUCTIONS;
        document.getElementById('instructions-p1').textContent = RES.CLICK_SEGMENT;
        document.getElementById('instructions-li1').textContent = RES.ARROW_1;
        document.getElementById('instructions-li2').textContent = RES.ARROW_2;
        document.getElementById('instructions-p2').textContent = RES.OFFSET_HINT;
    });

    let selectedSegment = null;

    // 页面加载时获取校准值
    window.onload = function () {
        fetchCalibrationData();
    };

    // 从 ESP32 获取校准值
    function fetchCalibrationData() {
        fetch('/get_calibration', {
            method: 'GET',
            headers: {
                'Content-Type': 'application/json'
            }
        })
        .then(response => response.json())
        .then(data => {
            updateSegments(data);
        })
        .catch(error => {
            console.error('Error fetching calibration data:', error);
        });
    }

    // 更新段码的偏移值
    function updateSegments(calibrationData) {
        Object.keys(calibrationData).forEach((digitKey, digitIndex) => {
            const segments = calibrationData[digitKey];
            const digitContainer = document.querySelector(`#digit-${digitIndex + 1}`);
            if (digitContainer) {
                digitContainer.querySelectorAll('.segment').forEach((segment, segmentIndex) => {
                    const offset = segments[segmentIndex] || 0;
                    segment.setAttribute('data-offset', offset);
                    segment.textContent = offset;
                });
            }
        });
    }

    // 添加点击事件以选中段码
    document.querySelectorAll('.segment').forEach(segment => {
        segment.addEventListener('click', () => {
            if (selectedSegment) {
                selectedSegment.classList.remove('selected');
            }
            selectedSegment = segment;
            selectedSegment.classList.add('selected');
        });
    });

    // 监听键盘事件
    document.addEventListener('keydown', (event) => {
        if (!selectedSegment) return;
        let offset = parseInt(selectedSegment.getAttribute('data-offset')) || 0;
        let change = 0;
        if (event.key === 'ArrowUp') {
            change = 1;
        } else if (event.key === 'ArrowDown') {
            change = -1;
        } else if (event.key === 'ArrowRight') {
            change = 5;
        } else if (event.key === 'ArrowLeft') {
            change = -5;
        } else {
            return;
        }
        offset += change;
        selectedSegment.setAttribute('data-offset', offset);
        selectedSegment.textContent = offset;
        sendCalibrationData();
    });

    // 发送校准数据到 ESP32
    function sendCalibrationData() {
        const calibrationData = { digit1: [], digit2: [], digit3: [], digit4: [] };
        document.querySelectorAll('.digit-container').forEach((digit, digitIndex) => {
            const segments = [];
            digit.querySelectorAll('.segment').forEach(segment => {
                segments.push(parseInt(segment.getAttribute('data-offset')) || 0);
            });
            calibrationData[`digit${digitIndex + 1}`] = segments;
        });
        fetch('/adjust', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(calibrationData)
        })
        .then(response => response.json())
        .then(data => {
            // 可选：处理返回
        })
        .catch(error => {
            console.error('Error sending calibration data:', error);
        });
    }
</script>
</body>
</html>



)rawliteral";

#endif