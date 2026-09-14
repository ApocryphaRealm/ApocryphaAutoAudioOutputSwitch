# -*- coding: utf-8 -*-
"""gen-translations.py - builds the eleven ApocryphaAutoAudioOutputSwitch_<language>.txt files.

The key list and the English text are read from source/UI.cpp (strings::TR("KEY", "text") and the log-level
arrays), so they cannot drift from the code. Strings this page shares word for word with Auto Draw's page reuse Auto
Draw's shipped translations (the log file name substituted); the strings new to this page are this project's own
translations, held below. A shared key whose English no longer matches Auto Draw's is an error, not a silent reuse.

Output: dist/Interface/Translations/ApocryphaAutoAudioOutputSwitch_<language>.txt - UTF-16LE with a BOM, one
"$key<TAB>text" per line, CRLF (the SKSE/SkyUI shape the framework reads).
"""
import codecs
import io
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AUTODRAW = os.path.join(os.path.dirname(REPO), "AutoDraw", "dist", "Interface", "Translations")
STEM = "ApocryphaAutoAudioOutputSwitch"
LANGS = ["english", "japanese", "korean", "chinese", "russian", "german", "french", "spanish", "italian", "polish", "czech"]

TR_RE = re.compile(r'strings::TR\(\s*"((?:[^"\\]|\\.)+)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)')
ARRAY_RE = r'constexpr const char\* {}\[\]\s*=\s*\{{([^}}]*)\}};'
LIT_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def unescape(s):
    return s.replace('\\\\', '\x00').replace('\\"', '"').replace('\x00', '\\')


def code_keys():
    src = io.open(os.path.join(REPO, "source", "UI.cpp"), encoding="utf-8").read()
    keys, order = {}, []
    for m in TR_RE.finditer(src):
        k, v = m.group(1), unescape(m.group(2))
        if k not in keys:
            order.append(k)
        keys[k] = v
    names = LIT_RE.findall(re.search(ARRAY_RE.format("kLogLevelNames"), src, re.S).group(1))
    lkeys = LIT_RE.findall(re.search(ARRAY_RE.format("kLogLevelKeys"), src, re.S).group(1))
    for k, v in zip(lkeys, names):
        if k not in keys:
            order.append(k)
        keys[k] = v
    return keys, order


def read_file(path):
    raw = io.open(path, "rb").read()
    text = raw[2:].decode("utf-16-le") if raw[:2] == codecs.BOM_UTF16_LE else raw.decode("utf-8-sig")
    out = {}
    for ln in text.replace("\r\n", "\n").split("\n"):
        if ln.startswith("$") and "\t" in ln:
            k, _, v = ln.partition("\t")
            out[k[1:]] = v
    return out


def swap_log(text):
    return text.replace("AutoDraw.log", STEM + ".log")


NEW = {
    "japanese": {
        "AAOS_Status": "状態",
        "AAOS_StatusInvalidated": "出力デバイスが使えなくなりました。接続されている別のデバイスに切り替えています...",
        "AAOS_StatusNotHooked": "ゲームのオーディオエンジンにフックできませんでした。理由はログを確認してください。",
        "AAOS_StatusNoEngine": "管理できるオーディオエンジンがありません。ゲーム起動時に使用可能な出力デバイスがありませんでした。",
        "AAOS_CurrentDevice": "再生中:",
        "AAOS_NoDevice": "出力デバイスが接続されていません。接続されるとすぐに切り替わります。",
        "AAOS_HelpCurrent": "現在ゲームの音声が出力されているデバイスです。",
        "AAOS_SwitchNowBtn": "今すぐ切り替え",
        "AAOS_StatusSwitching": "優先デバイスまたは既定のデバイスに切り替えています...",
        "AAOS_HelpSwitchNow": "変化がなくても、ゲームの音声を優先デバイス(未設定の場合はWindowsの既定のデバイス)に移します。",
        "AAOS_Output": "出力デバイス",
        "AAOS_Enabled": "出力デバイスを管理する",
        "AAOS_HelpEnabled": "オフにすると、このMODがない場合と同じく、ゲームは使用中のデバイスのままになります。",
        "AAOS_WindowsDefault": "Windowsの既定のデバイス",
        "AAOS_NotConnected": "(未接続)",
        "AAOS_Preferred": "優先デバイス",
        "AAOS_HelpPreferred": "接続されている間はこのデバイスを使い、再接続されるとこのデバイスに戻ります。「Windowsの既定のデバイス」はWindowsの設定に従います。",
        "AAOS_FollowDefault": "Windowsの既定のデバイスに従う",
        "AAOS_HelpFollowDefault": "優先デバイスが接続されていないとき、Windowsの既定の出力が変わるたびに切り替えます。オフにすると、現在のデバイスが取り外されるまでそのままです。",
        "AAOS_Delay": "切り替えの遅延",
        "AAOS_HelpDelay": "デバイスの変化から切り替えまでの待ち時間です。Windowsは1回の変化を複数のイベントとして通知するため、落ち着くまで待ちます。",
    },
    "korean": {
        "AAOS_Status": "상태",
        "AAOS_StatusInvalidated": "출력 장치가 작동을 멈췄습니다. 연결된 다른 장치로 전환하는 중...",
        "AAOS_StatusNotHooked": "게임의 오디오 엔진에 연결하지 못했습니다. 원인은 로그를 확인하세요.",
        "AAOS_StatusNoEngine": "관리할 오디오 엔진이 없습니다. 게임을 시작할 때 사용할 수 있는 출력 장치가 없었습니다.",
        "AAOS_CurrentDevice": "재생 장치:",
        "AAOS_NoDevice": "연결된 출력 장치가 없습니다. 장치가 연결되는 즉시 전환합니다.",
        "AAOS_HelpCurrent": "지금 게임 소리가 나오는 장치입니다.",
        "AAOS_SwitchNowBtn": "지금 전환",
        "AAOS_StatusSwitching": "기본 설정 장치 또는 기본 장치로 전환하는 중...",
        "AAOS_HelpSwitchNow": "변경된 것이 없어도 게임 소리를 기본 설정 장치로, 설정이 없으면 Windows 기본 장치로 옮깁니다.",
        "AAOS_Output": "출력 장치",
        "AAOS_Enabled": "출력 장치 관리",
        "AAOS_HelpEnabled": "끄면 이 모드가 없을 때와 똑같이 게임이 현재 사용 중인 장치를 계속 사용합니다.",
        "AAOS_WindowsDefault": "Windows 기본 장치",
        "AAOS_NotConnected": "(연결 안 됨)",
        "AAOS_Preferred": "기본 설정 장치",
        "AAOS_HelpPreferred": "이 장치가 연결되어 있으면 항상 사용하고, 다시 연결하면 이 장치로 돌아옵니다. Windows 기본 장치는 Windows 설정을 따릅니다.",
        "AAOS_FollowDefault": "Windows 기본 장치 따르기",
        "AAOS_HelpFollowDefault": "기본 설정 장치가 연결되어 있지 않을 때 Windows 기본 출력이 바뀔 때마다 전환합니다. 끄면 현재 장치가 제거될 때까지 유지합니다.",
        "AAOS_Delay": "전환 지연",
        "AAOS_HelpDelay": "장치가 바뀐 뒤 전환하기까지 기다리는 시간입니다. Windows는 한 번의 변경을 여러 이벤트로 알리므로 안정될 때까지 기다립니다.",
    },
    "chinese": {
        "AAOS_Status": "状态",
        "AAOS_StatusInvalidated": "输出设备已停止工作。正在切换到另一个已连接的设备...",
        "AAOS_StatusNotHooked": "无法挂接游戏的音频引擎。原因请查看日志。",
        "AAOS_StatusNoEngine": "没有可管理的音频引擎——游戏启动时没有可用的输出设备。",
        "AAOS_CurrentDevice": "正在播放：",
        "AAOS_NoDevice": "未连接输出设备。一旦有设备接入，游戏会立即切换。",
        "AAOS_HelpCurrent": "当前游戏声音输出到的设备。",
        "AAOS_SwitchNowBtn": "立即切换",
        "AAOS_StatusSwitching": "正在切换到首选设备或默认设备...",
        "AAOS_HelpSwitchNow": "即使没有任何变化，也把游戏声音移到首选设备；未设置首选设备时移到 Windows 默认设备。",
        "AAOS_Output": "输出设备",
        "AAOS_Enabled": "管理输出设备",
        "AAOS_HelpEnabled": "关闭后，游戏保持使用当前设备，与未安装此模组时完全相同。",
        "AAOS_WindowsDefault": "Windows 默认设备",
        "AAOS_NotConnected": "（未连接）",
        "AAOS_Preferred": "首选设备",
        "AAOS_HelpPreferred": "只要该设备已连接，游戏就使用它；重新插入后也会回到它。“Windows 默认设备”跟随 Windows 的设置。",
        "AAOS_FollowDefault": "跟随 Windows 默认设备",
        "AAOS_HelpFollowDefault": "首选设备未连接时，每当 Windows 默认输出改变就切换。关闭后，保持当前设备直到它被移除。",
        "AAOS_Delay": "切换延迟",
        "AAOS_HelpDelay": "设备变化后到切换前的等待时间。Windows 会把一次变化报告为多个事件，等待让它们稳定下来。",
    },
    "russian": {
        "AAOS_Status": "Состояние",
        "AAOS_StatusInvalidated": "Устройство вывода перестало работать. Переключение на другое подключённое устройство...",
        "AAOS_StatusNotHooked": "Не удалось перехватить звуковой движок игры. Причина указана в журнале.",
        "AAOS_StatusNoEngine": "Нет звукового движка для управления: при запуске игры не было доступного устройства вывода.",
        "AAOS_CurrentDevice": "Воспроизведение на:",
        "AAOS_NoDevice": "Устройство вывода не подключено. Игра переключится, как только оно появится.",
        "AAOS_HelpCurrent": "Устройство, на которое сейчас идёт звук игры.",
        "AAOS_SwitchNowBtn": "Переключить сейчас",
        "AAOS_StatusSwitching": "Переключение на предпочитаемое или стандартное устройство...",
        "AAOS_HelpSwitchNow": "Переводит звук игры на предпочитаемое устройство, а если оно не задано, на стандартное устройство Windows, даже если ничего не изменилось.",
        "AAOS_Output": "Устройство вывода",
        "AAOS_Enabled": "Управлять устройством вывода",
        "AAOS_HelpEnabled": "Выключено: игра остаётся на текущем устройстве, точно как без этого мода.",
        "AAOS_WindowsDefault": "Стандартное устройство Windows",
        "AAOS_NotConnected": "(не подключено)",
        "AAOS_Preferred": "Предпочитаемое устройство",
        "AAOS_HelpPreferred": "Игра использует это устройство, когда оно подключено, и возвращается к нему при повторном подключении. «Стандартное устройство Windows» следует настройке Windows.",
        "AAOS_FollowDefault": "Следовать стандартному устройству Windows",
        "AAOS_HelpFollowDefault": "Когда предпочитаемое устройство не подключено, переключаться при каждой смене стандартного вывода Windows. Выключено: оставаться на текущем устройстве, пока его не отключат.",
        "AAOS_Delay": "Задержка переключения",
        "AAOS_HelpDelay": "Сколько ждать после изменения устройств перед переключением. Windows сообщает об одном изменении несколькими событиями; ожидание даёт им завершиться.",
    },
    "german": {
        "AAOS_Status": "Status",
        "AAOS_StatusInvalidated": "Das Ausgabegerät funktioniert nicht mehr. Wechsel zu einem anderen verbundenen Gerät...",
        "AAOS_StatusNotHooked": "Die Audio-Engine des Spiels konnte nicht eingebunden werden. Den Grund nennt das Log.",
        "AAOS_StatusNoEngine": "Das Spiel hat keine Audio-Engine, die verwaltet werden kann – beim Start war kein Ausgabegerät nutzbar.",
        "AAOS_CurrentDevice": "Wiedergabe auf:",
        "AAOS_NoDevice": "Kein Ausgabegerät angeschlossen. Das Spiel wechselt, sobald eines erscheint.",
        "AAOS_HelpCurrent": "Das Gerät, auf dem der Ton des Spiels gerade ausgegeben wird.",
        "AAOS_SwitchNowBtn": "Jetzt wechseln",
        "AAOS_StatusSwitching": "Wechsel zum bevorzugten oder Standardgerät...",
        "AAOS_HelpSwitchNow": "Verlegt den Ton des Spiels auf das bevorzugte Gerät oder, wenn keines festgelegt ist, auf das Windows-Standardgerät – auch wenn sich nichts geändert hat.",
        "AAOS_Output": "Ausgabegerät",
        "AAOS_Enabled": "Ausgabegerät verwalten",
        "AAOS_HelpEnabled": "Aus lässt das Spiel auf dem Gerät, das es gerade nutzt – genau wie ohne diese Mod.",
        "AAOS_WindowsDefault": "Windows-Standardgerät",
        "AAOS_NotConnected": "(nicht verbunden)",
        "AAOS_Preferred": "Bevorzugtes Gerät",
        "AAOS_HelpPreferred": "Das Spiel nutzt dieses Gerät, sobald es verbunden ist, und kehrt beim erneuten Anschließen dorthin zurück. Windows-Standardgerät folgt der Einstellung von Windows.",
        "AAOS_FollowDefault": "Dem Windows-Standardgerät folgen",
        "AAOS_HelpFollowDefault": "Wenn das bevorzugte Gerät nicht verbunden ist, bei jeder Änderung der Windows-Standardausgabe wechseln. Aus bleibt auf dem aktuellen Gerät, bis es entfernt wird.",
        "AAOS_Delay": "Wechselverzögerung",
        "AAOS_HelpDelay": "Wartezeit nach einer Geräteänderung bis zum Wechsel. Windows meldet eine Änderung als mehrere Ereignisse; die Wartezeit lässt sie abklingen.",
    },
    "french": {
        "AAOS_Status": "État",
        "AAOS_StatusInvalidated": "Le périphérique de sortie a cessé de fonctionner. Bascule vers un autre périphérique connecté...",
        "AAOS_StatusNotHooked": "Impossible d'intercepter le moteur audio du jeu. Consultez le journal pour en connaître la raison.",
        "AAOS_StatusNoEngine": "Le jeu n'a aucun moteur audio à gérer : aucun périphérique de sortie n'était utilisable au lancement.",
        "AAOS_CurrentDevice": "Lecture sur :",
        "AAOS_NoDevice": "Aucun périphérique de sortie connecté. Le jeu bascule dès qu'il en apparaît un.",
        "AAOS_HelpCurrent": "Le périphérique sur lequel le son du jeu est diffusé en ce moment.",
        "AAOS_SwitchNowBtn": "Basculer maintenant",
        "AAOS_StatusSwitching": "Bascule vers le périphérique préféré ou par défaut...",
        "AAOS_HelpSwitchNow": "Envoie le son du jeu vers le périphérique préféré, ou vers le périphérique par défaut de Windows s'il n'y en a pas, même si rien n'a changé.",
        "AAOS_Output": "Périphérique de sortie",
        "AAOS_Enabled": "Gérer le périphérique de sortie",
        "AAOS_HelpEnabled": "Désactivé, le jeu reste sur le périphérique qu'il utilise, exactement comme sans ce mod.",
        "AAOS_WindowsDefault": "Périphérique par défaut de Windows",
        "AAOS_NotConnected": "(non connecté)",
        "AAOS_Preferred": "Périphérique préféré",
        "AAOS_HelpPreferred": "Le jeu utilise ce périphérique dès qu'il est connecté et y revient quand il est rebranché. « Périphérique par défaut de Windows » suit le réglage de Windows.",
        "AAOS_FollowDefault": "Suivre le périphérique par défaut de Windows",
        "AAOS_HelpFollowDefault": "Quand le périphérique préféré n'est pas connecté, basculer à chaque changement de la sortie par défaut de Windows. Désactivé, rester sur le périphérique actuel jusqu'à son retrait.",
        "AAOS_Delay": "Délai de bascule",
        "AAOS_HelpDelay": "Temps d'attente après un changement de périphérique avant de basculer. Windows signale un changement par plusieurs événements ; l'attente les laisse se stabiliser.",
    },
    "spanish": {
        "AAOS_Status": "Estado",
        "AAOS_StatusInvalidated": "El dispositivo de salida dejó de funcionar. Cambiando a otro dispositivo conectado...",
        "AAOS_StatusNotHooked": "No se pudo enganchar el motor de audio del juego. Consulta el registro para ver el motivo.",
        "AAOS_StatusNoEngine": "El juego no tiene un motor de audio que gestionar: no había ningún dispositivo de salida utilizable al iniciarse.",
        "AAOS_CurrentDevice": "Reproduciendo en:",
        "AAOS_NoDevice": "No hay ningún dispositivo de salida conectado. El juego cambiará en cuanto aparezca uno.",
        "AAOS_HelpCurrent": "El dispositivo al que va ahora el sonido del juego.",
        "AAOS_SwitchNowBtn": "Cambiar ahora",
        "AAOS_StatusSwitching": "Cambiando al dispositivo preferido o predeterminado...",
        "AAOS_HelpSwitchNow": "Lleva el sonido del juego al dispositivo preferido, o al dispositivo predeterminado de Windows si no hay ninguno, aunque no haya cambiado nada.",
        "AAOS_Output": "Dispositivo de salida",
        "AAOS_Enabled": "Gestionar el dispositivo de salida",
        "AAOS_HelpEnabled": "Desactivado, el juego se queda en el dispositivo que esté usando, igual que sin este mod.",
        "AAOS_WindowsDefault": "Dispositivo predeterminado de Windows",
        "AAOS_NotConnected": "(no conectado)",
        "AAOS_Preferred": "Dispositivo preferido",
        "AAOS_HelpPreferred": "El juego usa este dispositivo siempre que está conectado y vuelve a él al reconectarlo. «Dispositivo predeterminado de Windows» sigue la configuración de Windows.",
        "AAOS_FollowDefault": "Seguir el dispositivo predeterminado de Windows",
        "AAOS_HelpFollowDefault": "Cuando el dispositivo preferido no está conectado, cambiar cada vez que cambie la salida predeterminada de Windows. Desactivado, seguir en el dispositivo actual hasta que se retire.",
        "AAOS_Delay": "Retraso del cambio",
        "AAOS_HelpDelay": "Cuánto esperar tras un cambio de dispositivos antes de cambiar. Windows notifica un cambio como varios eventos; la espera deja que se asienten.",
    },
    "italian": {
        "AAOS_Status": "Stato",
        "AAOS_StatusInvalidated": "Il dispositivo di uscita ha smesso di funzionare. Passaggio a un altro dispositivo collegato...",
        "AAOS_StatusNotHooked": "Impossibile agganciare il motore audio del gioco. Il motivo è nel log.",
        "AAOS_StatusNoEngine": "Il gioco non ha un motore audio da gestire: all'avvio non c'era alcun dispositivo di uscita utilizzabile.",
        "AAOS_CurrentDevice": "In riproduzione su:",
        "AAOS_NoDevice": "Nessun dispositivo di uscita collegato. Il gioco passa a uno appena compare.",
        "AAOS_HelpCurrent": "Il dispositivo su cui va ora l'audio del gioco.",
        "AAOS_SwitchNowBtn": "Cambia ora",
        "AAOS_StatusSwitching": "Passaggio al dispositivo preferito o predefinito...",
        "AAOS_HelpSwitchNow": "Sposta l'audio del gioco sul dispositivo preferito, o sul dispositivo predefinito di Windows se non ne è impostato uno, anche se nulla è cambiato.",
        "AAOS_Output": "Dispositivo di uscita",
        "AAOS_Enabled": "Gestisci il dispositivo di uscita",
        "AAOS_HelpEnabled": "Disattivato, il gioco resta sul dispositivo che sta usando, esattamente come senza questa mod.",
        "AAOS_WindowsDefault": "Dispositivo predefinito di Windows",
        "AAOS_NotConnected": "(non collegato)",
        "AAOS_Preferred": "Dispositivo preferito",
        "AAOS_HelpPreferred": "Il gioco usa questo dispositivo ogni volta che è collegato e ci torna quando viene ricollegato. «Dispositivo predefinito di Windows» segue l'impostazione di Windows.",
        "AAOS_FollowDefault": "Segui il dispositivo predefinito di Windows",
        "AAOS_HelpFollowDefault": "Quando il dispositivo preferito non è collegato, cambia ogni volta che cambia l'uscita predefinita di Windows. Disattivato, resta sul dispositivo attuale finché non viene rimosso.",
        "AAOS_Delay": "Ritardo del cambio",
        "AAOS_HelpDelay": "Quanto attendere dopo un cambiamento dei dispositivi prima di cambiare. Windows segnala un cambiamento con più eventi; l'attesa li lascia stabilizzare.",
    },
    "polish": {
        "AAOS_Status": "Stan",
        "AAOS_StatusInvalidated": "Urządzenie wyjściowe przestało działać. Przełączanie na inne podłączone urządzenie...",
        "AAOS_StatusNotHooked": "Nie udało się podpiąć silnika dźwięku gry. Przyczynę podaje dziennik.",
        "AAOS_StatusNoEngine": "Gra nie ma silnika dźwięku do zarządzania – przy uruchomieniu nie było dostępnego urządzenia wyjściowego.",
        "AAOS_CurrentDevice": "Odtwarzanie na:",
        "AAOS_NoDevice": "Brak podłączonego urządzenia wyjściowego. Gra przełączy się, gdy tylko się pojawi.",
        "AAOS_HelpCurrent": "Urządzenie, na które teraz trafia dźwięk gry.",
        "AAOS_SwitchNowBtn": "Przełącz teraz",
        "AAOS_StatusSwitching": "Przełączanie na preferowane lub domyślne urządzenie...",
        "AAOS_HelpSwitchNow": "Przenosi dźwięk gry na preferowane urządzenie, a gdy go nie ustawiono – na domyślne urządzenie Windows, nawet jeśli nic się nie zmieniło.",
        "AAOS_Output": "Urządzenie wyjściowe",
        "AAOS_Enabled": "Zarządzaj urządzeniem wyjściowym",
        "AAOS_HelpEnabled": "Wyłączone zostawia grę na urządzeniu, którego używa – dokładnie jak bez tego moda.",
        "AAOS_WindowsDefault": "Domyślne urządzenie Windows",
        "AAOS_NotConnected": "(niepodłączone)",
        "AAOS_Preferred": "Preferowane urządzenie",
        "AAOS_HelpPreferred": "Gra używa tego urządzenia, gdy tylko jest podłączone, i wraca do niego po ponownym podłączeniu. „Domyślne urządzenie Windows” podąża za ustawieniem Windows.",
        "AAOS_FollowDefault": "Podążaj za domyślnym urządzeniem Windows",
        "AAOS_HelpFollowDefault": "Gdy preferowane urządzenie nie jest podłączone, przełączaj przy każdej zmianie domyślnego wyjścia Windows. Wyłączone zostaje na obecnym urządzeniu, dopóki nie zostanie odłączone.",
        "AAOS_Delay": "Opóźnienie przełączenia",
        "AAOS_HelpDelay": "Jak długo czekać po zmianie urządzeń przed przełączeniem. Windows zgłasza jedną zmianę jako kilka zdarzeń; oczekiwanie pozwala im się ustabilizować.",
    },
    "czech": {
        "AAOS_Status": "Stav",
        "AAOS_StatusInvalidated": "Výstupní zařízení přestalo fungovat. Přepínání na jiné připojené zařízení...",
        "AAOS_StatusNotHooked": "Zvukový engine hry se nepodařilo zachytit. Důvod je v logu.",
        "AAOS_StatusNoEngine": "Hra nemá žádný zvukový engine ke správě – při spuštění nebylo k dispozici žádné výstupní zařízení.",
        "AAOS_CurrentDevice": "Přehrává se na:",
        "AAOS_NoDevice": "Není připojeno žádné výstupní zařízení. Hra přepne, jakmile se nějaké objeví.",
        "AAOS_HelpCurrent": "Zařízení, na které teď jde zvuk hry.",
        "AAOS_SwitchNowBtn": "Přepnout teď",
        "AAOS_StatusSwitching": "Přepínání na upřednostňované nebo výchozí zařízení...",
        "AAOS_HelpSwitchNow": "Přesune zvuk hry na upřednostňované zařízení, a pokud není nastaveno, na výchozí zařízení Windows, i když se nic nezměnilo.",
        "AAOS_Output": "Výstupní zařízení",
        "AAOS_Enabled": "Spravovat výstupní zařízení",
        "AAOS_HelpEnabled": "Vypnuto ponechá hru na zařízení, které právě používá – přesně jako bez tohoto modu.",
        "AAOS_WindowsDefault": "Výchozí zařízení Windows",
        "AAOS_NotConnected": "(nepřipojeno)",
        "AAOS_Preferred": "Upřednostňované zařízení",
        "AAOS_HelpPreferred": "Hra používá toto zařízení, kdykoli je připojené, a po opětovném připojení se k němu vrátí. „Výchozí zařízení Windows“ se řídí nastavením Windows.",
        "AAOS_FollowDefault": "Řídit se výchozím zařízením Windows",
        "AAOS_HelpFollowDefault": "Když upřednostňované zařízení není připojené, přepnout při každé změně výchozího výstupu Windows. Vypnuto zůstane na současném zařízení, dokud nebude odebráno.",
        "AAOS_Delay": "Zpoždění přepnutí",
        "AAOS_HelpDelay": "Jak dlouho čekat po změně zařízení před přepnutím. Windows hlásí jednu změnu jako několik událostí; čekání je nechá ustálit.",
    },
}


def main():
    keys, order = code_keys()
    ad = {lang: read_file(os.path.join(AUTODRAW, f"AutoDraw_{lang}.txt")) for lang in LANGS}
    problems = []
    out_dir = os.path.join(REPO, "dist", "Interface", "Translations")
    os.makedirs(out_dir, exist_ok=True)
    for lang in LANGS:
        lines = []
        for k in order:
            english = keys[k]
            shared = "AD_" + k[len("AAOS_"):]
            if lang == "english":
                text = english
            elif k in NEW.get(lang, {}):
                text = NEW[lang][k]
            elif shared in ad["english"]:
                if swap_log(ad["english"][shared]) != english:
                    problems.append(f"{k}: English differs from Auto Draw's {shared}; translate it here instead")
                    continue
                if shared not in ad[lang]:
                    problems.append(f"{k}: Auto Draw has no {lang} text for {shared}")
                    continue
                text = swap_log(ad[lang][shared])
            else:
                problems.append(f"{k}: no {lang} translation")
                continue
            lines.append(f"${k}\t{text.replace(chr(10), chr(92) + 'n')}")
        data = codecs.BOM_UTF16_LE + ("\r\n".join(lines) + "\r\n").encode("utf-16-le")
        io.open(os.path.join(out_dir, f"{STEM}_{lang}.txt"), "wb").write(data)
        print(f"{lang}: {len(lines)} of {len(order)} keys")
    if problems:
        print("PROBLEMS:")
        for p in problems:
            print("  " + p)
        sys.exit(1)


if __name__ == "__main__":
    main()
