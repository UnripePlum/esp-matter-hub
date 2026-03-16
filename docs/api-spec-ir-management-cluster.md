# IrManagement 커스텀 Matter 클러스터 API 명세서

이 문서는 ESP32-S3 IR 허브 장치에 구현된 IrManagement 커스텀 Matter 클러스터(Cluster ID: `0x1337FC01`)를 사용하여 모바일 앱(iOS/Android)을 개발하는 개발자를 위한 가이드입니다.

## 1. 개요

IrManagement 클러스터는 제조사 전용 Matter 커스텀 클러스터입니다. 기존의 HTTP REST API 방식을 대체하여 Matter 프로토콜만으로 IR 학습, 신호 관리, 가상 장치 등록을 처리합니다.

이 클러스터는 브릿지 슬롯 엔드포인트(1번부터 8번)와 분리된 전용 엔드포인트에 위치합니다. 표준 Matter 클러스터(OnOff, LevelControl 등)는 브릿지 슬롯 엔드포인트에서 실제 장치 제어를 담당하며, 본 커스텀 클러스터는 허브의 관리 기능을 담당합니다.

## 2. 연결 방법

장치와 통신하기 위해서는 먼저 Matter 커미셔닝 과정을 거쳐야 합니다. BLE를 통해 Wi-Fi 또는 Thread 네트워크 정보를 전달하여 장치를 네트워크에 합류시킵니다.

커스텀 클러스터가 위치한 엔드포인트 ID는 동적으로 할당됩니다. 일반적으로 8개의 브릿지 슬롯 이후인 9번 엔드포인트에 할당되지만, 앱은 반드시 엔드포인트 목록을 탐색하여 클러스터 ID `0x1337FC01`을 가진 엔드포인트를 찾아야 합니다.

연동을 위해 Apple의 `Matter.framework`(MTRDevice) 또는 Android의 `Matter SDK`(ChipDeviceController)를 사용하세요.

## 3. 클러스터 식별

*   **Cluster ID**: `0x1337FC01` (Vendor Prefix: `0x1337`, Cluster: `0x0001`)
*   **Endpoint**: 동적 할당 (엔드포인트 목록에서 탐색 필요)

## 4. 속성 (Attributes)

모든 속성은 읽기 전용이며, 읽기 요청 시 실시간으로 계산된 값을 반환합니다.

| 속성 ID | 이름 | 타입 | 설명 |
| :--- | :--- | :--- | :--- |
| `0x0000` | LearnState | uint8 (enum) | IR 학습 상태: 0=IDLE, 1=IN_PROGRESS, 2=READY, 3=FAILED |
| `0x0001` | LearnedPayload | LongCharString (JSON) | 현재 학습 세션 상세 정보. 예: `{"state":0,"elapsed":0,"timeout":15000,"last_id":0,"rx":0,"len":0,"quality":0}` |
| `0x0002` | SavedSignalsList | LongCharString (JSON) | 저장된 모든 IR 신호 목록 (최대 8192바이트). JSON 배열 형태. |
| `0x0003` | SlotAssignments | LongCharString (JSON) | 브릿지 슬롯 할당 정보 (최대 2048바이트). JSON 배열 형태. |
| `0x0004` | RegisteredDevicesList | LongCharString (JSON) | 등록된 가상 장치 목록 (최대 4096바이트). JSON 배열 형태. |

## 5. 명령 (Commands)

모든 명령은 클라이언트에서 서버로 전송되며, 성공 시 `DefaultSuccessResponse`를 반환합니다.

### 0x00 StartLearning
IR 학습 세션을 시작합니다. 장치는 `IR_LEARNING_IN_PROGRESS` 상태로 진입합니다.
*   **필드**:
    *   `0`: `uint32 timeoutMs` (선택 사항, 기본값 15000)

### 0x01 CancelLearning
현재 진행 중인 학습을 취소합니다.
*   **참고**: 현재 구현에서는 `UNSUPPORTED` 상태를 반환합니다. 학습은 타임아웃에 의해 자동으로 종료됩니다.

### 0x02 SaveSignal
완료된 학습 세션에서 캡처한 IR 신호를 저장합니다.
*   **필드**:
    *   `0`: `string name` (최대 47자)
    *   `1`: `string deviceType` (최대 23자)

### 0x03 DeleteSignal
저장된 IR 신호를 삭제합니다. 해당 신호를 참조하던 장치의 연결 정보도 자동으로 해제됩니다.
*   **필드**:
    *   `0`: `uint32 signalId`

### 0x04 AssignSignalToDevice
등록된 장치의 동작(On/Off/Up/Down)에 IR 신호를 바인딩합니다.
*   **필드**:
    *   `0`: `uint32 deviceId`
    *   `1`: `uint32 onSignalId` (0은 할당 해제)
    *   `2`: `uint32 offSignalId`
    *   `3`: `uint32 upSignalId`
    *   `4`: `uint32 downSignalId`

### 0x05 RegisterDevice
새로운 가상 장치를 생성합니다.
*   **필드**:
    *   `0`: `string name` (최대 39자)
    *   `1`: `string deviceType` (선택 사항, 기본값 "light", 최대 15자)

### 0x06 RenameDevice
기존에 등록된 가상 장치의 이름을 변경합니다.
*   **필드**:
    *   `0`: `uint32 deviceId`
    *   `1`: `string name` (최대 39자)

### 0x07 AssignDeviceToSlot
등록된 장치를 브릿지 슬롯(0-7)에 할당합니다. 할당된 장치의 신호는 해당 슬롯의 Matter 엔드포인트에서 활성화됩니다.
*   **필드**:
    *   `0`: `uint8 slotId`
    *   `1`: `uint32 deviceId`

### 0x08 OpenCommissioningWindow
추가 컨트롤러가 Matter 패브릭에 참여할 수 있도록 커미셔닝 창을 엽니다.
*   **필드**:
    *   `0`: `uint16 timeoutSeconds` (선택 사항, 기본값 300)

## 6. 이벤트 (Events)

| 이벤트 ID | 이름 | 우선순위 | 설명 |
| :--- | :--- | :--- | :--- |
| `0x0000` | LearningCompleted | Info | IR 학습 세션이 성공하거나 타임아웃으로 종료될 때 발생합니다. |

## 7. 일반적인 사용 흐름

1.  **장치 커미셔닝**: 앱을 통해 허브를 네트워크에 연결합니다.
2.  **엔드포인트 탐색**: 커스텀 클러스터(`0x1337FC01`)가 있는 엔드포인트를 찾습니다.
3.  **IR 신호 학습**: `StartLearning` 명령을 보내고 `LearnState` 속성을 폴링하거나 이벤트를 대기합니다. 학습이 완료되면 `SaveSignal`로 저장합니다.
4.  **장치 구성**: `RegisterDevice`로 가상 장치를 만들고, `AssignSignalToDevice`로 학습한 신호를 연결합니다.
5.  **슬롯 할당**: `AssignDeviceToSlot`을 사용하여 장치를 슬롯에 배치합니다.
6.  **제어**: 슬롯에 해당하는 엔드포인트(1-8번)의 표준 Matter 클러스터를 사용하여 장치를 제어합니다.

## 8. chip-tool CLI 테스트

### 속성 읽기 예시
```bash
# 저장된 신호 목록 읽기
chip-tool any read-by-id 0x1337FC01 0x0002 <node-id> <endpoint-id>
```

### 명령 전송 예시
*   **StartLearning**: `chip-tool any command-by-id 0x1337FC01 0x00 '{"0:U32":15000}' <node-id> <endpoint-id>`
*   **CancelLearning**: `chip-tool any command-by-id 0x1337FC01 0x01 '{}' <node-id> <endpoint-id>`
*   **SaveSignal**: `chip-tool any command-by-id 0x1337FC01 0x02 '{"0:STR":"TV Power", "1:STR":"tv"}' <node-id> <endpoint-id>`
*   **DeleteSignal**: `chip-tool any command-by-id 0x1337FC01 0x03 '{"0:U32":1}' <node-id> <endpoint-id>`
*   **AssignSignalToDevice**: `chip-tool any command-by-id 0x1337FC01 0x04 '{"0:U32":1, "1:U32":1, "2:U32":2, "3:U32":0, "4:U32":0}' <node-id> <endpoint-id>`
*   **RegisterDevice**: `chip-tool any command-by-id 0x1337FC01 0x05 '{"0:STR":"My TV", "1:STR":"tv"}' <node-id> <endpoint-id>`
*   **RenameDevice**: `chip-tool any command-by-id 0x1337FC01 0x06 '{"0:U32":1, "1:STR":"Living Room TV"}' <node-id> <endpoint-id>`
*   **AssignDeviceToSlot**: `chip-tool any command-by-id 0x1337FC01 0x07 '{"0:U8":0, "1:U32":1}' <node-id> <endpoint-id>`
*   **OpenCommissioningWindow**: `chip-tool any command-by-id 0x1337FC01 0x08 '{"0:U16":300}' <node-id> <endpoint-id>`

## 9. 모바일 SDK 연동 가이드

### iOS (Matter.framework / MTRBaseDevice)

커스텀 클러스터는 표준 타입별 API(`MTRBaseClusterOnOff` 등)를 사용할 수 없습니다.
대신 `MTRBaseDevice`의 범용 메서드를 사용하여 클러스터 ID와 속성/명령 ID를 직접 지정합니다.

#### 엔드포인트 탐색

```swift
// Matter 커미셔닝 완료 후 MTRBaseDevice 인스턴스 획득
let device: MTRBaseDevice = ...

// Descriptor 클러스터(0x001D)의 PartsList(0x0003)을 root endpoint(0)에서 읽어 전체 엔드포인트 목록 확인
// 각 엔드포인트의 ServerList(0x0001)를 읽어 0x1337FC01 포함 여부로 대상 엔드포인트 식별
let irMgmtClusterId: UInt32 = 0x1337FC01
```

#### 속성 읽기

```swift
let endpointId = NSNumber(value: 9)  // 탐색으로 확인한 엔드포인트
let clusterId  = NSNumber(value: 0x1337FC01)
let attrId     = NSNumber(value: 0x0002)  // SavedSignalsList

let path = MTRAttributeRequestPath(
    endpointID: endpointId,
    clusterID:  clusterId,
    attributeID: attrId
)

device.readAttributes(withEndpointID: endpointId,
                       clusterID: clusterId,
                       attributeID: attrId,
                       params: nil) { values, error in
    guard let values = values as? [[String: Any]],
          let data = values.first?["data"] as? [String: Any],
          let jsonString = data["value"] as? String else { return }

    // JSON 파싱
    if let jsonData = jsonString.data(using: .utf8),
       let signals = try? JSONSerialization.jsonObject(with: jsonData) as? [[String: Any]] {
        print("저장된 신호: \(signals)")
    }
}
```

#### 명령 전송

```swift
// StartLearning (Command 0x00) — TLV 필드: {0: uint32 timeoutMs}
let fields: [String: Any] = [
    "type": "Structure",
    "value": [
        ["contextTag": 0, "type": "UnsignedInteger", "value": 15000]
    ]
]

device.invokeCommand(withEndpointID: NSNumber(value: 9),
                      clusterID: NSNumber(value: 0x1337FC01),
                      commandID: NSNumber(value: 0x00),
                      commandFields: fields as NSDictionary,
                      timedInvokeTimeout: nil) { values, error in
    if let error = error {
        print("StartLearning 실패: \(error)")
    } else {
        print("StartLearning 성공")
    }
}

// SaveSignal (Command 0x02) — TLV 필드: {0: string name, 1: string deviceType}
let saveFields: [String: Any] = [
    "type": "Structure",
    "value": [
        ["contextTag": 0, "type": "UTF8String", "value": "TV 전원"],
        ["contextTag": 1, "type": "UTF8String", "value": "tv"]
    ]
]

device.invokeCommand(withEndpointID: NSNumber(value: 9),
                      clusterID: NSNumber(value: 0x1337FC01),
                      commandID: NSNumber(value: 0x02),
                      commandFields: saveFields as NSDictionary,
                      timedInvokeTimeout: nil) { values, error in
    // 결과 처리
}
```

#### 이벤트 구독

```swift
// LearningCompleted 이벤트(0x0000) 구독
device.subscribeToEvents(withEndpointID: NSNumber(value: 9),
                          clusterID: NSNumber(value: 0x1337FC01),
                          eventID: NSNumber(value: 0x0000),
                          params: MTRSubscribeParams(minInterval: 1, maxInterval: 10)) { values, error in
    if values != nil {
        print("학습 완료 이벤트 수신")
        // LearnedPayload 속성 읽기로 결과 확인
    }
}
```

### Android (CHIP SDK / ChipDeviceController)

Android는 `ChipDeviceController`의 `invoke` 및 `readAttribute` 메서드를 사용합니다.
TLV 인코딩은 `TlvWriter`로 직접 구성합니다.

#### 속성 읽기

```kotlin
val controller: ChipDeviceController = ...
val endpointId = 9  // 탐색으로 확인한 엔드포인트
val clusterId = 0x1337FC01L
val attrId = 0x0002L  // SavedSignalsList

// AttributePath 구성
val path = ChipAttributePath.newInstance(endpointId.toLong(), clusterId, attrId)

controller.readPath(
    object : ReportCallback {
        override fun onReport(nodeState: NodeState) {
            val endpoint = nodeState.getEndpointState(endpointId)
            val cluster = endpoint?.getClusterState(clusterId)
            val attr = cluster?.getAttributeState(attrId)

            // TLV 디코딩 → UTF-8 문자열 → JSON 파싱
            val jsonString = attr?.value?.let { tlv ->
                TlvReader(tlv).getString(AnonymousTag)
            }
            val signals = JSONArray(jsonString)
            Log.d("IrMgmt", "저장된 신호: $signals")
        }
        override fun onError(attributePath: ChipAttributePath?, ex: Exception) {
            Log.e("IrMgmt", "읽기 실패", ex)
        }
    },
    devicePtr,
    listOf(path),
    0  // timeout
)
```

#### 명령 전송

```kotlin
// StartLearning (Command 0x00) — TLV 필드: {0: uint32 timeoutMs}
val tlvWriter = TlvWriter()
tlvWriter.startStructure(AnonymousTag)
tlvWriter.put(ContextSpecificTag(0), 15000u)  // timeoutMs
tlvWriter.endStructure()

val invokeElement = InvokeElement.newInstance(
    endpointId.toLong(),
    clusterId,
    0x00L,  // StartLearning command ID
    tlvWriter.getEncoded(),
    null
)

controller.invoke(
    object : InvokeCallback {
        override fun onResponse(element: InvokeElement?, successCode: Long) {
            Log.d("IrMgmt", "StartLearning 성공")
        }
        override fun onError(ex: Exception) {
            Log.e("IrMgmt", "StartLearning 실패", ex)
        }
    },
    devicePtr,
    invokeElement,
    0, 0  // timedRequest timeout, invoke timeout
)

// RegisterDevice (Command 0x05) — TLV 필드: {0: string name, 1: string deviceType}
val regTlv = TlvWriter()
regTlv.startStructure(AnonymousTag)
regTlv.put(ContextSpecificTag(0), "거실 TV")     // name
regTlv.put(ContextSpecificTag(1), "tv")           // deviceType
regTlv.endStructure()

val regElement = InvokeElement.newInstance(
    endpointId.toLong(), clusterId, 0x05L,
    regTlv.getEncoded(), null
)
// controller.invoke(callback, devicePtr, regElement, 0, 0)
```

> **참고**: 속성 값으로 반환되는 JSON 문자열은 앱 내에서 직접 파싱해야 합니다.
> iOS에서는 `JSONSerialization`, Android에서는 `org.json.JSONArray`/`JSONObject`를 사용하세요.

## 10. 에러 처리

명령 실행 결과는 Matter 상태 코드로 반환됩니다.

*   **Success (0x00)**: `ESP_OK` 반환 시. 명령이 성공적으로 수행되었습니다.
*   **InvalidCommand (0x85)**: `ESP_ERR_INVALID_ARG` 발생 시. 인자 값이 잘못되었습니다.
*   **UnsupportedCommand (0x81)**: `ESP_ERR_NOT_SUPPORTED` 발생 시. 지원하지 않는 명령입니다.
*   **Failure (0x01)**: `ESP_FAIL` 또는 기타 오류 발생 시.
