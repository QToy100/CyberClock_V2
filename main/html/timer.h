#ifndef _TIMER_HTML_
#define _TIMER_HTML_

const char timer_html[] = R"rawliteral(

<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>Timer Control</title>
    <style>
        body { background: #36393e; color: #bfbfbf; font-family: sans-serif; }
        .container {
            max-width: 1000px;
            width: 100%;
            margin: 60px auto;
            background: #232428;
            border-radius: 16px;
            padding: 48px 24px 36px 24px;
            box-shadow: 0 2px 12px #0003;
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        @media (max-width: 1000px) {
            .container {
                max-width: 100vw;
                width: 100vw;
                border-radius: 0;
                margin: 0;
                padding-left: 0;
                padding-right: 0;
            }
        }
        .round-btn {
            display: flex;
            align-items: center;
            justify-content: center;
            border: none;
            border-radius: 50%;
            background: #2675eb;
            color: #fff;
            font-weight: bold;
            cursor: pointer;
            box-shadow: 0 4px 16px #0002;
            transition: background 0.2s, box-shadow 0.2s, border 0.2s, color 0.2s;
            border: 6px solid transparent;
        }
        .round-btn:active {
            background: #1453b8;
            box-shadow: 0 2px 8px #0004;
        }
        .round-btn.hollow {
            background: transparent;
            color: #2675eb;
            border: 6px solid #2675eb;
        }
        /* Start按钮样式 */
        #mainBtn.round-btn {
            width: 80vw;
            height: 80vw;
            max-width: 600px;
            max-height: 600px;
            min-width: 140px;
            min-height: 140px;
            font-size: 5em;
            margin-bottom: 48px;
        }
        /* Reset按钮样式 */
        .reset-btn {
            width: 50vw;
            height: 50vw;
            max-width: 400px;
            max-height: 400px;
            min-width: 90px;
            min-height: 90px;
            font-size: 3.2em;
            margin-bottom: 0;
            background: #444;
        }
        .reset-btn:active {
            background: #222;
        }
        .timer-title {
            font-size: 2.2em;
            color: #2675eb;
            margin-bottom: 36px;
            margin-top: 0;
            font-weight: bold;
            letter-spacing: 2px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="timer-title" id="timer-title">Timer Control</div>
        <button id="mainBtn" class="round-btn" onclick="toggleTimer()">Start</button>
        <button id="resetBtn" class="round-btn reset-btn" onclick="resetTimer()">Reset</button>
    </div>
    <script>
        // 多语言资源
        const LANG_RES = {
            en: {
                TIMER_TITLE: "Timer Control",
                START: "Start",
                STOP: "Stop",
                RESET: "Reset"
            },
            zh: {
                TIMER_TITLE: "计时器控制",
                START: "开始",
                STOP: "停止",
                RESET: "重置"
            }
        };
        function getLang() {
            const lang = navigator.language || navigator.userLanguage || '';
            return lang.startsWith('zh') ? 'zh' : 'en';
        }
        const RES = LANG_RES[getLang()];

        // 设置多语言文本
        window.addEventListener('DOMContentLoaded', function() {
            document.getElementById('timer-title').textContent = RES.TIMER_TITLE;
            document.getElementById('mainBtn').textContent = RES.START;
            document.getElementById('resetBtn').textContent = RES.RESET;
        });

        let running = false;

        function toggleTimer() {
            const btn = document.getElementById('mainBtn');
            if (!running) {
                startTimer();
                btn.textContent = RES.STOP;
                btn.classList.add('hollow');
                running = true;
            } else {
                stopTimer();
                btn.textContent = RES.START;
                btn.classList.remove('hollow');
                running = false;
            }
        }

        function resetTimer() {
            resettimer();
            running = false;
            const btn = document.getElementById('mainBtn');
            btn.textContent = RES.START;
            btn.classList.remove('hollow');
        }

        function startTimer() {
            fetch('/set?timer=start').then(()=>{}).catch(()=>{});
        }
        function stopTimer() {
            fetch('/set?timer=stop').then(()=>{}).catch(()=>{});
        }
        function resettimer() {
            fetch('/set?timer=reset').then(()=>{}).catch(()=>{});
        }
    </script>
</body>
</html>

)rawliteral";

#endif