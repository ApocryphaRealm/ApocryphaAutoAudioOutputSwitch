# -*- coding: utf-8 -*-
"""gen-translations.py - builds the eleven ApocryphaAutoAudioInputSwitch_<language>.txt files.

The key list and the English text are read from source/UI.cpp (strings::TR("KEY", "text") and the log-level
arrays), so they cannot drift from the code. Strings this page shares word for word with Auto Draw's page reuse Auto
Draw's shipped translations (the log file name substituted); the strings new to this page are this project's own
translations, held below. A shared key whose English no longer matches Auto Draw's is an error, not a silent reuse.

Output: dist/Interface/Translations/ApocryphaAutoAudioInputSwitch_<language>.txt - UTF-16LE with a BOM, one
"$key<TAB>text" per line, CRLF (the SKSE/SkyUI shape the framework reads).
"""
import codecs
import io
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AUTODRAW = os.path.join(os.path.dirname(REPO), "AutoDraw", "dist", "Interface", "Translations")
STEM = "ApocryphaAutoAudioInputSwitch"
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
        "AAIS_Status": "状態",
        "AAIS_StatusNotHooked": "ゲームのオーディオエンジンにフックできませんでした。理由はログを確認してください。",
        "AAIS_StatusNoEngine": "管理できるオーディオエンジンがありません。ゲーム起動時に使用可能な出力デバイスがありませんでした。",
        "AAIS_CurrentDevice": "再生中:",
        "AAIS_NoDevice": "出力デバイスが接続されていません。接続されるとすぐに切り替わります。",
        "AAIS_HelpCurrent": "現在ゲームの音声が出力されているデバイスです。",
        "AAIS_SwitchNowBtn": "今すぐ切り替え",
        "AAIS_StatusSwitching": "優先デバイスまたは既定のデバイスに切り替えています...",
        "AAIS_HelpSwitchNow": "変化がなくても、ゲームの音声を優先デバイス(未設定の場合はWindowsの既定のデバイス)に移します。",
        "AAIS_Output": "出力デバイス",
        "AAIS_Enabled": "出力デバイスを管理する",
        "AAIS_HelpEnabled": "オフにすると、このMODがない場合と同じく、ゲームは使用中のデバイスのままになります。",
        "AAIS_WindowsDefault": "Windowsの既定のデバイス",
        "AAIS_NotConnected": "(未接続)",
        "AAIS_Preferred": "優先デバイス",
        "AAIS_HelpPreferred": "接続されている間はこのデバイスを使い、再接続されるとこのデバイスに戻ります。「Windowsの既定のデバイス」はWindowsの設定に従います。",
        "AAIS_FollowDefault": "Windowsの既定のデバイスに従う",
        "AAIS_HelpFollowDefault": "優先デバイスが接続されていないとき、Windowsの既定の出力が変わるたびに切り替えます。オフにすると、現在のデバイスが取り外されるまでそのままです。",
        "AAIS_Delay": "切り替えの遅延",
        "AAIS_HelpDelay": "デバイスの変化から切り替えまでの待ち時間です。Windowsは1回の変化を複数のイベントとして通知するため、落ち着くまで待ちます。",
    },
    "korean": {
        "AAIS_Status": "상태",
        "AAIS_StatusNotHooked": "게임의 오디오 엔진에 연결하지 못했습니다. 원인은 로그를 확인하세요.",
        "AAIS_StatusNoEngine": "관리할 오디오 엔진이 없습니다. 게임을 시작할 때 사용할 수 있는 출력 장치가 없었습니다.",
        "AAIS_CurrentDevice": "재생 장치:",
        "AAIS_NoDevice": "연결된 출력 장치가 없습니다. 장치가 연결되는 즉시 전환합니다.",
        "AAIS_HelpCurrent": "지금 게임 소리가 나오는 장치입니다.",
        "AAIS_SwitchNowBtn": "지금 전환",
        "AAIS_StatusSwitching": "기본 설정 장치 또는 기본 장치로 전환하는 중...",
        "AAIS_HelpSwitchNow": "변경된 것이 없어도 게임 소리를 기본 설정 장치로, 설정이 없으면 Windows 기본 장치로 옮깁니다.",
        "AAIS_Output": "출력 장치",
        "AAIS_Enabled": "출력 장치 관리",
        "AAIS_HelpEnabled": "끄면 이 모드가 없을 때와 똑같이 게임이 현재 사용 중인 장치를 계속 사용합니다.",
        "AAIS_WindowsDefault": "Windows 기본 장치",
        "AAIS_NotConnected": "(연결 안 됨)",
        "AAIS_Preferred": "기본 설정 장치",
        "AAIS_HelpPreferred": "이 장치가 연결되어 있으면 항상 사용하고, 다시 연결하면 이 장치로 돌아옵니다. Windows 기본 장치는 Windows 설정을 따릅니다.",
        "AAIS_FollowDefault": "Windows 기본 장치 따르기",
        "AAIS_HelpFollowDefault": "기본 설정 장치가 연결되어 있지 않을 때 Windows 기본 출력이 바뀔 때마다 전환합니다. 끄면 현재 장치가 제거될 때까지 유지합니다.",
        "AAIS_Delay": "전환 지연",
        "AAIS_HelpDelay": "장치가 바뀐 뒤 전환하기까지 기다리는 시간입니다. Windows는 한 번의 변경을 여러 이벤트로 알리므로 안정될 때까지 기다립니다.",
    },
    "chinese": {
        "AAIS_Status": "状态",
        "AAIS_StatusNotHooked": "无法挂接游戏的音频引擎。原因请查看日志。",
        "AAIS_StatusNoEngine": "没有可管理的音频引擎——游戏启动时没有可用的输出设备。",
        "AAIS_CurrentDevice": "正在播放：",
        "AAIS_NoDevice": "未连接输出设备。一旦有设备接入，游戏会立即切换。",
        "AAIS_HelpCurrent": "当前游戏声音输出到的设备。",
        "AAIS_SwitchNowBtn": "立即切换",
        "AAIS_StatusSwitching": "正在切换到首选设备或默认设备...",
        "AAIS_HelpSwitchNow": "即使没有任何变化，也把游戏声音移到首选设备；未设置首选设备时移到 Windows 默认设备。",
        "AAIS_Output": "输出设备",
        "AAIS_Enabled": "管理输出设备",
        "AAIS_HelpEnabled": "关闭后，游戏保持使用当前设备，与未安装此模组时完全相同。",
        "AAIS_WindowsDefault": "Windows 默认设备",
        "AAIS_NotConnected": "（未连接）",
        "AAIS_Preferred": "首选设备",
        "AAIS_HelpPreferred": "只要该设备已连接，游戏就使用它；重新插入后也会回到它。“Windows 默认设备”跟随 Windows 的设置。",
        "AAIS_FollowDefault": "跟随 Windows 默认设备",
        "AAIS_HelpFollowDefault": "首选设备未连接时，每当 Windows 默认输出改变就切换。关闭后，保持当前设备直到它被移除。",
        "AAIS_Delay": "切换延迟",
        "AAIS_HelpDelay": "设备变化后到切换前的等待时间。Windows 会把一次变化报告为多个事件，等待让它们稳定下来。",
    },
    "russian": {
        "AAIS_Status": "Состояние",
        "AAIS_StatusNotHooked": "Не удалось перехватить звуковой движок игры. Причина указана в журнале.",
        "AAIS_StatusNoEngine": "Нет звукового движка для управления: при запуске игры не было доступного устройства вывода.",
        "AAIS_CurrentDevice": "Воспроизведение на:",
        "AAIS_NoDevice": "Устройство вывода не подключено. Игра переключится, как только оно появится.",
        "AAIS_HelpCurrent": "Устройство, на которое сейчас идёт звук игры.",
        "AAIS_SwitchNowBtn": "Переключить сейчас",
        "AAIS_StatusSwitching": "Переключение на предпочитаемое или стандартное устройство...",
        "AAIS_HelpSwitchNow": "Переводит звук игры на предпочитаемое устройство, а если оно не задано, на стандартное устройство Windows, даже если ничего не изменилось.",
        "AAIS_Output": "Устройство вывода",
        "AAIS_Enabled": "Управлять устройством вывода",
        "AAIS_HelpEnabled": "Выключено: игра остаётся на текущем устройстве, точно как без этого мода.",
        "AAIS_WindowsDefault": "Стандартное устройство Windows",
        "AAIS_NotConnected": "(не подключено)",
        "AAIS_Preferred": "Предпочитаемое устройство",
        "AAIS_HelpPreferred": "Игра использует это устройство, когда оно подключено, и возвращается к нему при повторном подключении. «Стандартное устройство Windows» следует настройке Windows.",
        "AAIS_FollowDefault": "Следовать стандартному устройству Windows",
        "AAIS_HelpFollowDefault": "Когда предпочитаемое устройство не подключено, переключаться при каждой смене стандартного вывода Windows. Выключено: оставаться на текущем устройстве, пока его не отключат.",
        "AAIS_Delay": "Задержка переключения",
        "AAIS_HelpDelay": "Сколько ждать после изменения устройств перед переключением. Windows сообщает об одном изменении несколькими событиями; ожидание даёт им завершиться.",
    },
    "german": {
        "AAIS_Status": "Status",
        "AAIS_StatusNotHooked": "Die Audio-Engine des Spiels konnte nicht eingebunden werden. Den Grund nennt das Log.",
        "AAIS_StatusNoEngine": "Das Spiel hat keine Audio-Engine, die verwaltet werden kann – beim Start war kein Ausgabegerät nutzbar.",
        "AAIS_CurrentDevice": "Wiedergabe auf:",
        "AAIS_NoDevice": "Kein Ausgabegerät angeschlossen. Das Spiel wechselt, sobald eines erscheint.",
        "AAIS_HelpCurrent": "Das Gerät, auf dem der Ton des Spiels gerade ausgegeben wird.",
        "AAIS_SwitchNowBtn": "Jetzt wechseln",
        "AAIS_StatusSwitching": "Wechsel zum bevorzugten oder Standardgerät...",
        "AAIS_HelpSwitchNow": "Verlegt den Ton des Spiels auf das bevorzugte Gerät oder, wenn keines festgelegt ist, auf das Windows-Standardgerät – auch wenn sich nichts geändert hat.",
        "AAIS_Output": "Ausgabegerät",
        "AAIS_Enabled": "Ausgabegerät verwalten",
        "AAIS_HelpEnabled": "Aus lässt das Spiel auf dem Gerät, das es gerade nutzt – genau wie ohne diese Mod.",
        "AAIS_WindowsDefault": "Windows-Standardgerät",
        "AAIS_NotConnected": "(nicht verbunden)",
        "AAIS_Preferred": "Bevorzugtes Gerät",
        "AAIS_HelpPreferred": "Das Spiel nutzt dieses Gerät, sobald es verbunden ist, und kehrt beim erneuten Anschließen dorthin zurück. Windows-Standardgerät folgt der Einstellung von Windows.",
        "AAIS_FollowDefault": "Dem Windows-Standardgerät folgen",
        "AAIS_HelpFollowDefault": "Wenn das bevorzugte Gerät nicht verbunden ist, bei jeder Änderung der Windows-Standardausgabe wechseln. Aus bleibt auf dem aktuellen Gerät, bis es entfernt wird.",
        "AAIS_Delay": "Wechselverzögerung",
        "AAIS_HelpDelay": "Wartezeit nach einer Geräteänderung bis zum Wechsel. Windows meldet eine Änderung als mehrere Ereignisse; die Wartezeit lässt sie abklingen.",
    },
    "french": {
        "AAIS_Status": "État",
        "AAIS_StatusNotHooked": "Impossible d'intercepter le moteur audio du jeu. Consultez le journal pour en connaître la raison.",
        "AAIS_StatusNoEngine": "Le jeu n'a aucun moteur audio à gérer : aucun périphérique de sortie n'était utilisable au lancement.",
        "AAIS_CurrentDevice": "Lecture sur :",
        "AAIS_NoDevice": "Aucun périphérique de sortie connecté. Le jeu bascule dès qu'il en apparaît un.",
        "AAIS_HelpCurrent": "Le périphérique sur lequel le son du jeu est diffusé en ce moment.",
        "AAIS_SwitchNowBtn": "Basculer maintenant",
        "AAIS_StatusSwitching": "Bascule vers le périphérique préféré ou par défaut...",
        "AAIS_HelpSwitchNow": "Envoie le son du jeu vers le périphérique préféré, ou vers le périphérique par défaut de Windows s'il n'y en a pas, même si rien n'a changé.",
        "AAIS_Output": "Périphérique de sortie",
        "AAIS_Enabled": "Gérer le périphérique de sortie",
        "AAIS_HelpEnabled": "Désactivé, le jeu reste sur le périphérique qu'il utilise, exactement comme sans ce mod.",
        "AAIS_WindowsDefault": "Périphérique par défaut de Windows",
        "AAIS_NotConnected": "(non connecté)",
        "AAIS_Preferred": "Périphérique préféré",
        "AAIS_HelpPreferred": "Le jeu utilise ce périphérique dès qu'il est connecté et y revient quand il est rebranché. « Périphérique par défaut de Windows » suit le réglage de Windows.",
        "AAIS_FollowDefault": "Suivre le périphérique par défaut de Windows",
        "AAIS_HelpFollowDefault": "Quand le périphérique préféré n'est pas connecté, basculer à chaque changement de la sortie par défaut de Windows. Désactivé, rester sur le périphérique actuel jusqu'à son retrait.",
        "AAIS_Delay": "Délai de bascule",
        "AAIS_HelpDelay": "Temps d'attente après un changement de périphérique avant de basculer. Windows signale un changement par plusieurs événements ; l'attente les laisse se stabiliser.",
    },
    "spanish": {
        "AAIS_Status": "Estado",
        "AAIS_StatusNotHooked": "No se pudo enganchar el motor de audio del juego. Consulta el registro para ver el motivo.",
        "AAIS_StatusNoEngine": "El juego no tiene un motor de audio que gestionar: no había ningún dispositivo de salida utilizable al iniciarse.",
        "AAIS_CurrentDevice": "Reproduciendo en:",
        "AAIS_NoDevice": "No hay ningún dispositivo de salida conectado. El juego cambiará en cuanto aparezca uno.",
        "AAIS_HelpCurrent": "El dispositivo al que va ahora el sonido del juego.",
        "AAIS_SwitchNowBtn": "Cambiar ahora",
        "AAIS_StatusSwitching": "Cambiando al dispositivo preferido o predeterminado...",
        "AAIS_HelpSwitchNow": "Lleva el sonido del juego al dispositivo preferido, o al dispositivo predeterminado de Windows si no hay ninguno, aunque no haya cambiado nada.",
        "AAIS_Output": "Dispositivo de salida",
        "AAIS_Enabled": "Gestionar el dispositivo de salida",
        "AAIS_HelpEnabled": "Desactivado, el juego se queda en el dispositivo que esté usando, igual que sin este mod.",
        "AAIS_WindowsDefault": "Dispositivo predeterminado de Windows",
        "AAIS_NotConnected": "(no conectado)",
        "AAIS_Preferred": "Dispositivo preferido",
        "AAIS_HelpPreferred": "El juego usa este dispositivo siempre que está conectado y vuelve a él al reconectarlo. «Dispositivo predeterminado de Windows» sigue la configuración de Windows.",
        "AAIS_FollowDefault": "Seguir el dispositivo predeterminado de Windows",
        "AAIS_HelpFollowDefault": "Cuando el dispositivo preferido no está conectado, cambiar cada vez que cambie la salida predeterminada de Windows. Desactivado, seguir en el dispositivo actual hasta que se retire.",
        "AAIS_Delay": "Retraso del cambio",
        "AAIS_HelpDelay": "Cuánto esperar tras un cambio de dispositivos antes de cambiar. Windows notifica un cambio como varios eventos; la espera deja que se asienten.",
    },
    "italian": {
        "AAIS_Status": "Stato",
        "AAIS_StatusNotHooked": "Impossibile agganciare il motore audio del gioco. Il motivo è nel log.",
        "AAIS_StatusNoEngine": "Il gioco non ha un motore audio da gestire: all'avvio non c'era alcun dispositivo di uscita utilizzabile.",
        "AAIS_CurrentDevice": "In riproduzione su:",
        "AAIS_NoDevice": "Nessun dispositivo di uscita collegato. Il gioco passa a uno appena compare.",
        "AAIS_HelpCurrent": "Il dispositivo su cui va ora l'audio del gioco.",
        "AAIS_SwitchNowBtn": "Cambia ora",
        "AAIS_StatusSwitching": "Passaggio al dispositivo preferito o predefinito...",
        "AAIS_HelpSwitchNow": "Sposta l'audio del gioco sul dispositivo preferito, o sul dispositivo predefinito di Windows se non ne è impostato uno, anche se nulla è cambiato.",
        "AAIS_Output": "Dispositivo di uscita",
        "AAIS_Enabled": "Gestisci il dispositivo di uscita",
        "AAIS_HelpEnabled": "Disattivato, il gioco resta sul dispositivo che sta usando, esattamente come senza questa mod.",
        "AAIS_WindowsDefault": "Dispositivo predefinito di Windows",
        "AAIS_NotConnected": "(non collegato)",
        "AAIS_Preferred": "Dispositivo preferito",
        "AAIS_HelpPreferred": "Il gioco usa questo dispositivo ogni volta che è collegato e ci torna quando viene ricollegato. «Dispositivo predefinito di Windows» segue l'impostazione di Windows.",
        "AAIS_FollowDefault": "Segui il dispositivo predefinito di Windows",
        "AAIS_HelpFollowDefault": "Quando il dispositivo preferito non è collegato, cambia ogni volta che cambia l'uscita predefinita di Windows. Disattivato, resta sul dispositivo attuale finché non viene rimosso.",
        "AAIS_Delay": "Ritardo del cambio",
        "AAIS_HelpDelay": "Quanto attendere dopo un cambiamento dei dispositivi prima di cambiare. Windows segnala un cambiamento con più eventi; l'attesa li lascia stabilizzare.",
    },
    "polish": {
        "AAIS_Status": "Stan",
        "AAIS_StatusNotHooked": "Nie udało się podpiąć silnika dźwięku gry. Przyczynę podaje dziennik.",
        "AAIS_StatusNoEngine": "Gra nie ma silnika dźwięku do zarządzania – przy uruchomieniu nie było dostępnego urządzenia wyjściowego.",
        "AAIS_CurrentDevice": "Odtwarzanie na:",
        "AAIS_NoDevice": "Brak podłączonego urządzenia wyjściowego. Gra przełączy się, gdy tylko się pojawi.",
        "AAIS_HelpCurrent": "Urządzenie, na które teraz trafia dźwięk gry.",
        "AAIS_SwitchNowBtn": "Przełącz teraz",
        "AAIS_StatusSwitching": "Przełączanie na preferowane lub domyślne urządzenie...",
        "AAIS_HelpSwitchNow": "Przenosi dźwięk gry na preferowane urządzenie, a gdy go nie ustawiono – na domyślne urządzenie Windows, nawet jeśli nic się nie zmieniło.",
        "AAIS_Output": "Urządzenie wyjściowe",
        "AAIS_Enabled": "Zarządzaj urządzeniem wyjściowym",
        "AAIS_HelpEnabled": "Wyłączone zostawia grę na urządzeniu, którego używa – dokładnie jak bez tego moda.",
        "AAIS_WindowsDefault": "Domyślne urządzenie Windows",
        "AAIS_NotConnected": "(niepodłączone)",
        "AAIS_Preferred": "Preferowane urządzenie",
        "AAIS_HelpPreferred": "Gra używa tego urządzenia, gdy tylko jest podłączone, i wraca do niego po ponownym podłączeniu. „Domyślne urządzenie Windows” podąża za ustawieniem Windows.",
        "AAIS_FollowDefault": "Podążaj za domyślnym urządzeniem Windows",
        "AAIS_HelpFollowDefault": "Gdy preferowane urządzenie nie jest podłączone, przełączaj przy każdej zmianie domyślnego wyjścia Windows. Wyłączone zostaje na obecnym urządzeniu, dopóki nie zostanie odłączone.",
        "AAIS_Delay": "Opóźnienie przełączenia",
        "AAIS_HelpDelay": "Jak długo czekać po zmianie urządzeń przed przełączeniem. Windows zgłasza jedną zmianę jako kilka zdarzeń; oczekiwanie pozwala im się ustabilizować.",
    },
    "czech": {
        "AAIS_Status": "Stav",
        "AAIS_StatusNotHooked": "Zvukový engine hry se nepodařilo zachytit. Důvod je v logu.",
        "AAIS_StatusNoEngine": "Hra nemá žádný zvukový engine ke správě – při spuštění nebylo k dispozici žádné výstupní zařízení.",
        "AAIS_CurrentDevice": "Přehrává se na:",
        "AAIS_NoDevice": "Není připojeno žádné výstupní zařízení. Hra přepne, jakmile se nějaké objeví.",
        "AAIS_HelpCurrent": "Zařízení, na které teď jde zvuk hry.",
        "AAIS_SwitchNowBtn": "Přepnout teď",
        "AAIS_StatusSwitching": "Přepínání na upřednostňované nebo výchozí zařízení...",
        "AAIS_HelpSwitchNow": "Přesune zvuk hry na upřednostňované zařízení, a pokud není nastaveno, na výchozí zařízení Windows, i když se nic nezměnilo.",
        "AAIS_Output": "Výstupní zařízení",
        "AAIS_Enabled": "Spravovat výstupní zařízení",
        "AAIS_HelpEnabled": "Vypnuto ponechá hru na zařízení, které právě používá – přesně jako bez tohoto modu.",
        "AAIS_WindowsDefault": "Výchozí zařízení Windows",
        "AAIS_NotConnected": "(nepřipojeno)",
        "AAIS_Preferred": "Upřednostňované zařízení",
        "AAIS_HelpPreferred": "Hra používá toto zařízení, kdykoli je připojené, a po opětovném připojení se k němu vrátí. „Výchozí zařízení Windows“ se řídí nastavením Windows.",
        "AAIS_FollowDefault": "Řídit se výchozím zařízením Windows",
        "AAIS_HelpFollowDefault": "Když upřednostňované zařízení není připojené, přepnout při každé změně výchozího výstupu Windows. Vypnuto zůstane na současném zařízení, dokud nebude odebráno.",
        "AAIS_Delay": "Zpoždění přepnutí",
        "AAIS_HelpDelay": "Jak dlouho čekat po změně zařízení před přepnutím. Windows hlásí jednu změnu jako několik událostí; čekání je nechá ustálit.",
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
            shared = "AD_" + k[len("AAIS_"):]
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
