# MeshCore CZ

> **English summary** — This is a Czech community fork of [MeshCore](https://github.com/ripplebiz/MeshCore), built on repeater firmware **v1.16.0**. It is *not* a new protocol or a separate network: it is plain MeshCore with a small, openly maintained set of Czech defaults and fixes (radio settings for the CZ Narrow band, regional flood scopes, advert metadata). Upstream documentation, clients, and the getting-started guide live at [meshcore.io](https://meshcore.io) and [docs.meshcore.io](https://docs.meshcore.io). The rest of this README — describing the goal and the CZ-specific options — is in Czech.

---

# Jednotný český firmware pro repeatery

## Problém dnes

Každý repeater může běžet na jiné verzi firmware, s jinými opravami a jiným nastavením.

Když se objeví chyba nebo nová užitečná funkce, každý ji řeší zvlášť. Často také není jasné, která verze je ověřená a která pouze experimentální.

## Návrh

Vytvořit společný český firmware builder založený na oficiálním MeshCore firmware.

Builder by automaticky připravoval známé a přehledně označené verze pro repeatery v české síti.

Nešlo by o nový protokol ani oddělenou síť. Pořád by to byl MeshCore, pouze se společně spravovanými úpravami.

## Co obsahuje na začátku

První verze je záměrně minimální:

* metadata o repeateru (advert metadata),
* vybrané opravy známých chyb,
* společné radiové výchozí hodnoty pro českou síť (CZ Narrow),
* regionální flood scope (`cz` a kraje),
* jasný seznam změn proti oficiální verzi.

## Stabilní a testovací verze

Cílem je vydávat více variant:

**Stable** — doporučená verze pro dlouhodobý provoz repeateru.

**Testing** — verze s novou opravou nebo funkcí, kterou dobrovolně otestuje několik provozovatelů.

Díky tomu nebudeme testovat náhodně. Budeme vědět, které repeatery používají konkrétní verzi a jaké jsou výsledky.

## Proč je to užitečné pro provozovatele

* nemusí si firmware sestavovat sám,
* jednoduše pozná doporučenou verzi,
* dostane opravy, na které by jinak čekal,
* může se dobrovolně zapojit do testování,
* při problému bude jasné, co na repeateru běží,
* bude možné snadno přejít na jinou nebo oficiální verzi.

## Přínos pro celou síť

Když budeme nové verze nasazovat koordinovaně, můžeme změny nejdříve ověřit na několika repeaterech a až potom je doporučit ostatním.

To znamená méně náhodných úprav, rychlejší hledání problémů a lepší přehled o stavu české sítě.

## Hlavní myšlenka

> Nechceme, aby všechny repeatery musely běžet na jednom povinném firmware. Chceme mít jeden důvěryhodný a koordinovaný způsob, jak připravovat stabilní i testovací verze pro ty, kteří je chtějí používat.

---

# Technický přehled změn

## Radiové výchozí hodnoty (CZ Narrow)

Výchozí build flagy v `platformio.ini` (sekce `[arduino_base]`):

| Nastavení | Hodnota |
|-----------|---------|
| Frekvence | 869.432 MHz |
| Šířka pásma | 62.5 kHz |
| Spreading factor | 7 |
| Coding rate | 4/5 |
| Vysílací výkon | 22 dBm |
| Kódování cesty | 2 bajty (`DEFAULT_PATH_HASH_MODE=1`) |
| Duty cycle | 10 % (`DEFAULT_AIRTIME_FACTOR=9.0`) |
| Výchozí flood scope | `cz` |

Jednotlivé desky mohou ve svém `variants/*/platformio.ini` přepsat `LORA_TX_POWER` nebo radiové piny.

## Regionální presety

Při `MESHCORE_CZ_REGION_PRESET=1` (ve výchozím stavu zapnuto) repeatery, room servery i senzory při prvním startu vytvoří strom regionů, pokud ještě neexistuje soubor `/regions2`:

- **`cz`** — celostátní scope; flood povolen
- **14 krajů** — potomci `cz`, flood ve výchozím stavu zakázán

Kraje: `cz-pha`, `cz-stc`, `cz-jhc`, `cz-plz`, `cz-kvk`, `cz-ulk`, `cz-lbk`, `cz-hkk`, `cz-pak`, `cz-vys`, `cz-jmk`, `cz-olk`, `cz-zlk`, `cz-msk`.

Preferovaný flood scope je `*, cz` (nescopované zprávy a provoz se scope `cz`). Krajské regiony jsou definované, ale neforwardují se, dokud je výslovně nepovolíte.

## Rozšířená advert metadata

Repeater může v advertu vysílat dvě 16bitová pole se schopnostmi zařízení (`feat1` a `feat2`). `feat1` je identifikátor schématu, `feat2` je bitová maska schopností (např. solární napájení, záložní baterie, připojení k internetu, brána, typ antény). Hodnoty se zadávají desítkově nebo v hexu s prefixem `0x` a ukládají se do samostatného souboru `/cz_advert` (vlastní magic + verze), takže nehrozí kolize s formátem sdílených NodePrefs.

Hodnoty nastavíte přes Remote Admin CLI:

```
set advert.features <feat1> <feat2>
```

Aktuální nastavení zjistíte příkazem:

```
get advert.features
```

Hodnoty `feat1` a `feat2` si pohodlně sestavíte v online generátoru, který zaškrtnutí jednotlivých schopností převede na výsledný příkaz:

**https://meshcore-cz.github.io/meshcore-metadata-advert-generator/**

Příklad — pro `feat1 = 0x4E01` a `feat2 = 0x0060` (zapnuté bity 5 a 6) generátor vytvoří příkaz:

```
set advert.features 0x4E01 0x0060
```

Poznámka: `feat1` je povinný, pokud nastavujete `feat2`.

## Sestavení (build)

Vyžaduje [PlatformIO](https://platformio.org/). Výpis prostředí:

```bash
pio project config
```

Build a upload příkladu (název prostředí podle desky):

```bash
pio run -e SenseCap_Solar_repeater -t upload
```

### Lokální přepisy

Pro nastavení specifická pro konkrétní stroj vytvořte `platformio.local.ini` (necommituje se). Globální sekce `[env]` přidá flagy do všech prostředí, takže není nutné měnit `platformio.ini`, např. nRF52 debug:

```ini
[env]
build_flags =
  -D CFG_DEBUG=0
```

## Rozložení větví

| Větev | Účel |
|-------|------|
| `repeater-1.16.0-cz` | Integrační větev CZ firmware (cíl PR) |
| `cz-core` | Radiové výchozí hodnoty a regionální presety |
| `cz-advert-features` | CLI pro advert metadata |

Základní tag: `repeater-v1.16.0`.

## Licence

Stejná jako upstream MeshCore — licence MIT.
