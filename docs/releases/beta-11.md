# MWR VR — Beta 11

Unofficial VR mod for the **Modern Warfare Remastered campaign**, based on
Overlord by Aeka0. This is a test build. The following changes are relative to
the published MWR VR Beta 10, not upstream Overlord's version numbers.

## Changes since Beta 10

### Weapons and interaction

- **Automatic reload option:** enable automatic reload in Weapon interaction
  to use the game's reload action and gun-part animation. Physical reload
  remains the default. Launcher rocket loading remains physical; the controller
  hands do not automatically follow the animated magazine.
- **More reliable weapon release and pickup:** a blocked drop retries while
  the same weapon remains released; gripping again cancels that retry. Fixed
  the training-table G36C becoming ungrabbable after an automatic pickup attempt.
- **Reload part visibility:** corrected retained-pose/input-gap handling that
  could hide an independent receiver or return a continuously held magazine.
  In busy War Pig exteriors, the shared scene surface arena now supports
  **512 KiB instead of 256 KiB**, with matching model-address calculations.
  This addresses recorded allocation failures affecting replacement magazines,
  chamber rounds and M4 receivers.
- **Original Overlord AK magazine attachment option:** choose the upstream
  insertion path instead of the MWR hook-and-rock gesture. Default off;
  restart after changing it. Real ammunition and latch checks remain active.
- **M203 handling:** Trigger can pinch the tube independently of the support
  grip. Loaded rounds follow the tube; a fired case falls once it loses support.
  M4/M16 attachment geometry uses the correct MWR seated-round position.
- **W1200 casing extraction:** casing ejection follows committed manual pump
  extraction rather than the initial shot.
- **C4:** the left-hand detonator remains usable after selecting a rifle.
- **Back holster feedback:** a small readiness pulse when a hand enters a usable
  back slot, alongside the existing draw/stow confirmation.

### Camera, movement and mounted guns

- **Room-scale walking:** horizontal physical head movement can move the player
  body through the game's movement/collision system. Stick movement retains
  priority; collision can still prevent the body from following the headset.
- **Vehicle cameras:** improved carrier translation/heading tracking for the
  Shock and Awe Mk19 and Charlie Don't Surf helicopter passenger sequences.
- **Mounted aim and seating:** corrected Mk19 horizontal aim direction, centres
  the entering head at the gun seat, and establishes neutral handles relative
  to that entry head position. Subsequent head movement remains independent.
- **Heat minigun:** camera movement is separated from gun aim, and missing grip
  hands are restored. The Shock and Awe Mk19 uses its native hands without an
  extra independent VR pair.
- **Cargoship hints:** selected crouch/stand/grenade/mantle prompts use VR
  instructions instead of desktop key names.

### Rendering and startup

- **AA stability:** preserves saved SMAA T2x/Filmic settings after hardware
  optimization in both native D3D11 and DXVK. Improves resource recovery during
  AA changes and keeps projection jitter aligned with each eye's history phase.
- **Loading stability:** shader preloading is locked off to avoid the reproduced
  native D3D11 loading/device-removal path. Expanded the hardware-survey heap
  that overflowed after accepting optimization while using DXVK.
- **OpenXR image waits:** timeout retries preserve image ownership and defer a
  frame instead of immediately treating the timeout as session failure.
- **UI/FOV compatibility:** removes arbitrary menu-size and optical-range limits
  while keeping actual texture/device limits and validity checks.
- **MWR host isolation:** excludes unported H2-specific host providers and
  compiler services from the MWR target; shared code keeps explicit H1 bindings.

### Launcher and personal settings

- **Separate hand angles:** adjust physical left/right controller angles
  independently, with separate standard/legacy controller setting banks.
- **First-person size:** scale hands, weapons and associated reload items
  together from **20–100%**. Default 100%; **restart required**.
- **Tracer styles:** Native, Blaze, Cerulean, Royal, Anime, 1776 and Rainbow.
- **Hold/toggle weapon grip:** choose whether the weapon stays held after
  releasing Grip. Reload items retain their physical controls.
- **T-pose measurement:** clearer guide, two-second continuous two-trigger hold,
  invalid-pose/release reset, and saved height/arm-length fitting.
- **Separate VR profile:** VR settings and progress use
  `overlord-mwr/profile/players2`. Existing VR progress wins; first use seeds the
  profile from the original game. Steam Cloud behaviour is unchanged.
- Launcher and executable display **Beta 11**, file version **0.1.0.11**.

## Validation and remaining checks

The changes have focused CPU/GPU tests and campaign runs with a simulated
headset in native D3D11 and DXVK. The scene-arena correction completed **50,147
replacement model builds with zero failures**, above the former 256 KiB limit.
Captures show M4 receivers and detached AK magazines; chamber-only firing,
magazine rotation/walking and low-frame-rate pause/resume were checked with
native god mode enabled.

These results do not establish real-headset comfort, controller fit/haptics or
compatibility with every mission, runtime or GPU. In particular, busy-exterior
visibility, room-scale collision feel, mounted-camera comfort, T-pose fit and
new interaction options still need headset acceptance. Temporal AA can still
shimmer on fine detail. The reported 5–10 minute loading delay was not reproduced.

## Installation

- `MWR-VR-Beta-11-Setup.exe`: run and select the owned MWR installation.
- `MWR-VR-Beta-11.zip`: extract into the folder containing `h1_sp64_ship.exe`.
- Start `overlord.exe`. Keep the complete bundled folder structure when updating.
- `MWR-VR-Beta-11-source.zip`: matching working-source snapshot with dependencies
  and license notices. PDBs are retained separately for diagnosis.

Original game files, personal saves/settings, captures and logs are not included.

---

# MWR VR — 베타 11 변경 사항

이 목록은 **MWR VR 베타 10 이후**의 변경 사항입니다. 실험 버전이며,
모의 헤드셋 테스트를 통과한 부분과 실제 헤드셋 확인이 필요한 부분을 구분합니다.

## 총기와 상호작용

- **자동 재장전 옵션:** 무기 상호작용 설정에서 게임의 재장전 동작과 총기 부품
  애니메이션을 사용할 수 있습니다. 기본은 수동 재장전이며, 로켓 장전은 계속
  수동입니다. 손은 자동으로 탄창 애니메이션을 따라가지 않습니다.
- **총기 놓기·줍기 개선:** 장애물 때문에 놓지 못한 총기는 놓기 상태에서 재시도하고,
  다시 잡으면 재시도를 취소합니다. 훈련장 탁자의 G36C를 잡지 못하는 경로를 수정했습니다.
- **대체 모델 사라짐 수정:** 포즈 갱신이나 입력 지연 중 총몸이 사라지거나 쥔 탄창이
  반환되는 경로를 수정했습니다. 워 피그 도심에서 확인된 버퍼 초과에 대응해 장면
  버퍼를 **256→512KB**로 늘리고 모델 주소 계산도 함께 수정했습니다.
- **원본 Overlord 방식 AK 탄창 결합 옵션:** MWR의 걸고 젖히는 동작 대신 원본의
  삽입 경로를 선택할 수 있습니다. 기본 꺼짐, 변경 후 **재시작이 필요**합니다.
- **M203:** 보조 손잡이와 별개로 Trigger를 사용해 튜브를 집을 수 있습니다.
  탄이 튜브를 따라 움직이고, 지지를 잃은 빈 탄피가 한 번만 떨어집니다.
- **W1200:** 발사 직후 대신 실제 펌프 추출 시점에 탄피를 배출합니다.
- **C4:** 소총 선택 후에도 왼손 기폭기를 사용할 수 있습니다.
- **등 홀스터:** 사용할 수 있는 슬롯에 손을 넣으면 짧은 준비 진동을 줍니다.

## 카메라·이동·기관총

- 실제 수평 머리 이동에 맞춰 게임의 이동·충돌 처리를 통해 몸이 따라오는
  룸스케일 이동을 추가했습니다. 스틱 이동이 우선하며, 장애물은 몸의 이동을 막습니다.
- Shock and Awe의 Mk19와 Charlie Don't Surf의 헬기 탑승 카메라가 차량의
  이동과 방향을 따라가는 처리를 개선했습니다.
- Mk19 좌우 조준 방향, 탑승 시 머리 위치와 중립 손잡이 위치를 수정했습니다.
  탑승 후 머리 움직임은 조준과 독립적으로 유지됩니다.
- Heat 미니건의 카메라가 조준에 끌려가는 현상과 손이 보이지 않는 문제를 수정했습니다.
  Shock and Awe에서는 원래 손에 추가 VR 손이 겹치지 않도록 했습니다.
- Cargoship의 일부 자세·수류탄·넘기 안내를 VR 조작 안내로 바꿨습니다.

## 화면과 실행 안정성

- 하드웨어 최적화 후에도 저장된 T2x/Filmic AA 설정을 유지하고, AA 변경 시
  자원 복구와 양쪽 눈의 샘플·히스토리 타이밍을 개선했습니다.
- 재현된 D3D11 로딩 오류를 피하도록 셰이더 사전 로딩을 껐습니다.
  DXVK 최적화 확인 후 발생하던 하드웨어 조사 메모리 부족도 수정했습니다.
- OpenXR 이미지 대기 시간 초과 시 소유권을 유지하며 다음 프레임에서 재시도합니다.
- 메뉴 크기와 시야 범위의 임의 제한을 정리하고, MWR 전용 코드와 미이식 H2 코드를 분리했습니다.

## 개인 설정과 런처

- 실제 왼손·오른손의 컨트롤러 각도를 따로 조정할 수 있습니다.
- 손·무기·재장전 부품 크기를 **20~100%**로 조정할 수 있습니다.
  기본 100%, **재시작이 필요**합니다.
- 예광탄 스타일 7종과 총기 Grip 유지/토글 방식을 선택할 수 있습니다.
- T 포즈 안내, 양쪽 Trigger 2초 유지 측정, 자세 이탈 시 초기화와 팔 길이 저장을 개선했습니다.
- VR 설정과 진행 상황을 `overlord-mwr/profile/players2`에 따로 보관합니다.
  기존 VR 진행 상황이 우선하며, 최초 사용 시 원래 게임 프로필에서 복사합니다.
- 런처와 실행 파일을 **Beta 11 / 0.1.0.11**로 변경했습니다.

## 확인 범위와 설치

기본 D3D11·DXVK 모의 테스트와 단위 테스트를 수행했습니다. 버퍼 수정은 기존 한도를
넘긴 상태에서 모델 생성 **50,147회 중 실패 0회**였으며, 무적 상태에서 총몸·분리 탄창,
약실 1발 발사와 저프레임 후 복귀를 확인했습니다.

실제 헤드셋의 장시간 안정성, 탑승감, 손 맞춤과 진동은 추가 확인이 필요합니다.
잔잔한 무늬의 AA 반짝임이 모두 없어지는 것은 아니며, 제보된 5~10분 로딩 지연은
재현하지 못했습니다.

설치 프로그램을 실행하거나 수동 설치 ZIP을 `h1_sp64_ship.exe`가 있는 폴더에 풀고
`overlord.exe`를 실행하세요. 원본 게임·개인 세이브·설정·로그는 배포 파일에 포함하지 않습니다.
