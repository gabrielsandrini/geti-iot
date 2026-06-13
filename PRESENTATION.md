# Geti IoT / HydroSense — Roteiro de Apresentação (Técnica)

**Seminário:** ~15 minutos
**Disciplina:** Pós-graduação em Gestão Estratégica de TI
**Tema:** Arquitetura técnica do monitoramento e controle de hidroponia com IoT (ESP32 + Blynk Cloud)

> **Pré-requisito:** os conceitos de hidroponia (pH, EC/nutrientes, nível do tanque e por que
> precisam ficar na faixa ideal) já foram apresentados no seminário anterior. **Esta
> apresentação é técnica** — não reintroduz hidroponia; foca na arquitetura, no firmware, no
> protocolo de dados e nas decisões de engenharia.
>
> Como usar este roteiro: cada seção é um slide. **No slide** = o que projetar (poucas palavras
> / o trecho de código); **Roteiro de fala** = o que você diz.

## Índice e tempos

| # | Slide | ⏱ |
|---|-------|----|
| 1 | Capa & escopo técnico | 0:30 |
| 2 | Arquitetura em 3 camadas | 1:30 |
| 3 | Mapa de dados — pinos virtuais (V0–V10) | 1:30 |
| 4 | Decisão #1 — inteligência na borda | 1:30 |
| 5 | O laço de controle (ler → decidir → atuar) | 1:30 |
| 6 | Histerese: a zona morta | 1:30 |
| 7 | Válvula multifunção & guarda anti-transbordo | 1:30 |
| 8 | Configuração em runtime — JSON no V10 | 1:30 |
| 9 | O cliente web: polling, read-only, fonte única | 1:00 |
| 10 | Segurança: token, segredos e exposição | 1:00 |
| 11 | Trade-offs técnicos | 1:00 |
| 12 | Demonstração & encerramento | 1:00 |
| | **Total** | **15:00** |

---

## Slide 1 — Capa & escopo técnico ⏱ 0:30

**No slide**

- **Geti IoT / HydroSense** — mergulho técnico
- ESP32 (firmware C++) · Blynk Cloud (datastreams) · dashboard web (HTML + HTTP API)
- *Hidroponia já foi vista — aqui é arquitetura e código*
- *Gabrielle Oliveira Santana · Gabriel Sandrini · Igor Murici*

**Roteiro de fala**
> Na apresentação anterior vimos **o que** é a hidroponia e **por que** pH, condutividade e nível
> precisam de controle. Hoje eu vou direto ao **como**: a arquitetura do sistema, o firmware que
> roda no ESP32, o protocolo de dados na nuvem e as decisões de engenharia por trás de cada parte.
> Não vou reintroduzir hidroponia — vou mostrar **o código e a arquitetura** que a controlam.

---

## Slide 2 — Arquitetura em 3 camadas ⏱ 1:30

**No slide**

```
[ BORDA ]            [ NUVEM ]              [ CLIENTE ]
  ESP32      --2s-->  Blynk Cloud   <--5s--   HydroSense
  firmware  publica   datastreams    consulta  index.html
  C++ (.ino)          (pinos V0–V10)  HTTP API  (1 arquivo)
   │                                              │
   └─ lê 5 sensores, decide, aciona 4 atuadores   └─ monitora + edita limites
```

- **Borda:** ESP32 — DHT22 (temp/umid) + 3 potenciômetros (nível/pH/EC) que emulam sensores que a plataforma wokwi não disponibiliza + 4 LEDs emulando bombas/válvula
- **Nuvem:** Blynk gerenciada — *datastreams* em pinos virtuais, API HTTP REST
- **Cliente:** SPA de **um arquivo HTML**, fala direto na API HTTP — **sem backend**
- Cadências assimétricas: device **publica a cada 2 s**; app **consulta a cada 5 s**

**Roteiro de fala**
> O sistema tem três camadas com responsabilidades bem separadas. Na **borda**, o ESP32 roda um
> firmware em C++ — lê cinco sensores, executa a lógica de controle e aciona quatro atuadores. Na
> simulação Wokwi, o DHT22 é real, e nível, pH e EC são potenciômetros; os atuadores são LEDs.
>
> Na **nuvem**, a Blynk é uma plataforma gerenciada que expõe os dados como *datastreams* em pinos
> virtuais, acessíveis por uma API HTTP REST. Na camada **cliente**, o HydroSense é uma SPA de um
> único arquivo HTML que conversa **direto** com essa API — não há backend próprio, não há servidor
> de aplicação.
>
> Reparem nas **cadências assimétricas**: o dispositivo publica a cada 2 segundos, o app consulta a
> cada 5. São relógios independentes, sem handshake — e isso só funciona porque, como vou mostrar, a
> decisão **não** depende do app estar sincronizado.

---

## Slide 3 — Mapa de dados: pinos virtuais (V0–V10) ⏱ 1:30

**No slide**

| Pino | Conteúdo | Tipo | Direção |
|------|----------|------|---------|
| V0–V1 | temperatura, umidade | Double | device → nuvem |
| V2–V4 | nível, pH, EC | Double | device → nuvem |
| V5–V8 | nutriente, ácido, base, válvula | Integer 0/1 | device → nuvem |
| **V10** | **limites de controle (JSON)** | **String** | **app → device** |

- A *interface* entre device e app é o conjunto de pinos — um **contrato de dados**
- Sensores e atuadores fluem **device → nuvem**; só os **limites** fluem **app → device**
- O *device* é o único **escritor** dos atuadores (V5–V8); o app só **lê**

**Roteiro de fala**
> Toda a comunicação passa por esse mapa de pinos virtuais — é o **contrato de dados** do sistema.
> V0 a V4 são as leituras dos sensores, que o dispositivo publica na nuvem. V5 a V8 são o estado
> dos quatro atuadores, como inteiros 0 ou 1. E o V10, o mais importante arquiteturalmente, é uma
> **String** que carrega os limites de controle em JSON.
>
> O ponto-chave é a **direção do fluxo**: sensores e estado de atuadores vão sempre do dispositivo
> para a nuvem; o dispositivo é o **único escritor** desses pinos. A única coisa que viaja no
> sentido contrário — do app para o dispositivo — são os **limites**, no V10. Essa assimetria de
> quem-escreve-o-quê é o que define a arquitetura inteira, e é o assunto dos próximos slides.

---

## Slide 4 — Decisão #1: a inteligência fica na borda ⏱ 1:30

**No slide**

```cpp
void sendSensors() {
  float level = analogRead(PIN_LEVEL)/4095.0*100.0;
  float ph    = analogRead(PIN_PH)/4095.0*14.0;
  float ppm   = analogRead(PIN_EC)/4095.0*2000.0;
  Blynk.virtualWrite(V2, level); /* V3, V4 … */

  autoActuate(ph, ppm, level);   // ← o device decide, todo ciclo
}
```

- A lógica de decisão roda **no firmware**, dentro do loop de 2 s — **não no app**
- *Defaults* seguros embarcados (`phLo=5.5 … tankFull=95`) → autorregula no 1º boot
- Funciona **sem internet e sem app aberto** → o app não é dependência operacional
- Alternativa rejeitada: app decide e comanda → cai a rede, a planta fica sem controle

**Roteiro de fala**
> Esta é a decisão arquitetural central. Olhem a função `sendSensors`, que roda a cada 2 segundos:
> ela lê os sensores, publica na nuvem **e** — na última linha — chama `autoActuate`. Ou seja, **o
> próprio dispositivo decide** e atua, dentro do mesmo ciclo, sem perguntar nada para ninguém.
>
> O firmware ainda traz **valores-padrão embarcados** — a faixa de pH, de EC, os limites do tanque.
> Então, mesmo que o app nunca conecte, o dispositivo se autorregula desde o primeiro boot. A
> alternativa — colocar a lógica no app, que lê e manda ligar a bomba — é mais fácil de programar,
> mas cria uma dependência fatal: se cai a rede ou ninguém está com o app aberto, a planta fica sem
> controle. Colocando a decisão na borda, eu **reduzo a superfície de falha**: o caminho crítico não
> passa pela internet.

---

## Slide 5 — O laço de controle: ler → decidir → atuar ⏱ 1:30

**No slide**

```cpp
void autoActuate(float ph, float ec, float level) {
  if      (ph > phHi + phWarn) setActuator(PIN_ACID, stAcid, true,  …);
  else if (ph <= phHi)         setActuator(PIN_ACID, stAcid, false, …);
  // pH baixo → base ; EC baixo → nutriente (mesma forma)
  …
}

void setActuator(pin, &state, on, vpin, name, why) {
  if (state == on) return;            // ← change guard: sem chatter, sem eco
  state = on; digitalWrite(pin, on);
  Blynk.virtualWrite(vpin, on?1:0);   // espelha estado na nuvem
}
```

- `setActuator` centraliza atuar + espelhar na nuvem + logar **o motivo** (`[ACT]`)
- **Change guard** (`if state==on return`): evita repetição e o eco `virtualWrite → BLYNK_WRITE`
- Regras: pH alto→ácido · pH baixo→base · EC baixo→nutriente

**Roteiro de fala**
> O controle é um laço simples — ler, decidir, atuar — mas a engenharia está nos detalhes. As
> decisões são `if/else` diretos: pH acima do limite liga o ácido, abaixo liga a base, EC baixo liga
> o nutriente.
>
> O cuidado fica na função `setActuator`, que **toda** decisão chama. Ela faz três coisas: aciona o
> pino físico, **espelha** o estado na nuvem com `virtualWrite`, e **loga o motivo** numa linha
> `[ACT]`. E repare na primeira linha: `if (state == on) return`. Esse **change guard** é essencial
> por dois motivos. Primeiro, evita *chatter* — só age quando o estado realmente muda. Segundo,
> quebra um **loop de eco**: como o `virtualWrite` para a nuvem pode disparar de volta o `BLYNK_WRITE`
> do mesmo pino, sem essa guarda o sistema entraria em laço infinito de escritas. Uma linha resolve
> os dois problemas.

---

## Slide 6 — Histerese: a zona morta ⏱ 1:30

**No slide**

```cpp
// pH: liga ácido no CRÍTICO, desliga só ao VOLTAR à faixa ideal
if      (ph > phHi + phWarn) setActuator(PIN_ACID, stAcid, true,  …); // liga
else if (ph <= phHi)         setActuator(PIN_ACID, stAcid, false, …); // desliga
//        ^^^^^^ ^^^^^^^^^
//        limiar de liga ≠ limiar de desliga  →  banda morta = phWarn
```

- **Sem histerese:** liga e desliga no mesmo ponto → atuador "pisca" perto do limite (desgaste)
- **Com histerese:** liga em `phHi + phWarn`, desliga em `phHi` → **banda morta** dá estabilidade
- Mesmo padrão para base (`phLo`), nutriente (`ecLo`) — `*Warn` é a largura da banda

**Roteiro de fala**
> Esse slide mostra o truque de controle mais importante: a **histerese**. Olhem os dois limiares.
> O ácido **liga** quando o pH passa de `phHi + phWarn` — o ponto crítico — mas só **desliga** quando
> o pH volta a `phHi`, o topo da faixa ideal. Os dois limiares são **diferentes de propósito**.
>
> Por quê? Se eu ligasse e desligasse no mesmo valor, qualquer ruído do sensor em torno do limite
> faria o atuador **piscar** — liga, desliga, liga, desliga — desgastando a bomba e desestabilizando a
> solução. A diferença entre os dois limiares cria uma **banda morta**, de largura `phWarn`: uma vez
> ligado, o atuador só relaxa depois de uma recuperação real, não a cada flutuação. É o mesmo padrão
> usado num termostato. Aplico exatamente isso à base, com `phLo`, e ao nutriente, com `ecLo` — a
> margem `*Warn` é, literalmente, a largura da zona morta de cada variável.

---

## Slide 7 — Válvula multifunção & guarda anti-transbordo ⏱ 1:30

**No slide**

```cpp
// Ordem importa: a guarda de segurança é avaliada PRIMEIRO
if      (level >= tankFull)           setActuator(PIN_WATER,…,false,"tanque cheio");
else if (level < tankCrit)            setActuator(PIN_WATER,…,true, "tanque baixo");
else if (ec > ecHi + ecWarn)          setActuator(PIN_WATER,…,true, "EC alto (diluir)");
else if (level >= tankWarn && ec<=ecHi) setActuator(PIN_WATER,…,false,"ok");
```

- A válvula tem **duas funções**: reabastecer tanque baixo **ou** diluir EC alto
- **Guarda anti-transbordo**: `level >= tankFull` é o **primeiro** `if` → trava de segurança
- Prioridade no código = prioridade de risco: **diluir nunca pode transbordar**

**Roteiro de fala**
> O caso mais rico é a válvula de água, porque ela tem **dupla função** e um conflito potencial.
> Função um: reabastecer quando o tanque está baixo. Função dois: **diluir** a solução quando a EC
> está alta demais — adicionar água baixa a concentração. Mas as duas podem brigar: e se a EC está
> alta (quero abrir para diluir) e o tanque já está cheio (não posso adicionar água)?
>
> A resposta está na **ordem dos `if`**. A condição `level >= tankFull` é a **primeira** avaliada —
> antes de qualquer lógica de negócio. Se o tanque está cheio, a válvula fecha e ponto, mesmo que a
> EC ainda esteja alta. A **prioridade no código reflete a prioridade de risco**: uma trava de
> segurança — não transbordar — tem precedência sobre o objetivo operacional — diluir. Em engenharia
> de sistemas críticos, isso se chama *fail-safe*, e aqui ele é literalmente a primeira linha da função.

---

## Slide 8 — Configuração em runtime: JSON no V10 ⏱ 1:30

**No slide**

```cpp
BLYNK_WRITE(V10){                                 // app escreveu os limites
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, param.asStr())) return; // JSON inválido → mantém limites
  phLo = doc["phLo"] | phLo;                       // '| phLo' = fallback se faltar a chave
  ecHi = doc["ecHi"] | ecHi;  /* … 9 limites … */
}
// payload: {"phLo":5.5,"phHi":6.5,"ecLo":600,"ecHi":1000,"tankCrit":20,"tankFull":95,…}
```

- Problema: mudar a faixa ideal **sem regravar/recompilar o firmware**
- Solução: todos os limites num **único JSON atômico**, no pino V10
- App é a **fonte da verdade**; device adota em **1 ciclo**; `BLYNK_CONNECTED` faz `syncVirtual(V10)`
- 1 string > 9 pinos separados: atômico, extensível (nova chave ≠ novo pino), à prova de estado parcial

**Roteiro de fala**
> Se a inteligência está na borda, surge um problema operacional: como mudar uma faixa ideal — o pH
> de uma nova cultura — **sem recompilar e regravar o firmware** toda vez? A solução é o handler
> `BLYNK_WRITE(V10)`. Quando o app salva, o dispositivo recebe uma **String JSON** com todos os
> limites e a desserializa com a ArduinoJson.
>
> Dois detalhes de robustez. Primeiro, se o JSON for inválido ou vazio, ele dá `return` e **mantém os
> limites atuais** — nunca adota lixo. Segundo, aquele operador `| phLo`: é um **fallback** — se uma
> chave faltar no JSON, mantém o valor que já tinha. Então um JSON parcial é seguro.
>
> Por que **um JSON** em vez de nove pinos, um por limite? Porque é **atômico**: todos os limites
> mudam juntos, num único write, sem janela de estado inconsistente. E é **extensível**: adicionar um
> limite novo é adicionar uma chave — não criar um datastream e reconfigurar a nuvem. No reboot, o
> `BLYNK_CONNECTED` chama `syncVirtual(V10)` e restaura os limites da nuvem. É a diferença entre
> **reconfigurar** e **reimplantar**.

---

## Slide 9 — O cliente web: polling, read-only, fonte única ⏱ 1:00

**No slide**

```js
const POLL_MS = 5000;
async function poll(){
  const d = await (await fetch(`${BLYNK}/getAll?token=${TOKEN}`)).json();
  // V0–V4 → cards/charts ; V5–V8 → estado read-only ; V10 → form de limites
  Object.keys(ACT).forEach(k => syncActuatorFromPin(k, num(ACT[k].pin)));
}
```

- App **lê** tudo via `getAll` a cada 5 s; **escreve só** o V10 (limites)
- **Atuadores são read-only** no app — mostram estado, não ligam/desligam
- Evita dois "donos" disputando a mesma bomba → **fonte única de comando** (o device)
- SPA sem framework, sem build: `fetch` + manipulação de DOM puro

**Roteiro de fala**
> O cliente é deliberadamente magro. A função `poll` roda a cada 5 segundos: um `getAll` traz todos
> os pinos de uma vez; V0–V4 alimentam os cards e gráficos, V5–V8 viram o estado **somente leitura**
> dos atuadores, e o V10 preenche o formulário de limites.
>
> Decisão de projeto: o app **não tem botão** para ligar atuador. Ele observa e parametriza, mas
> **não comanda**. Se ele também pudesse comandar, teríamos dois donos do mesmo atuador — app manda
> ligar, device acha que deve desligar — e o sistema oscilaria. Centralizar a atuação numa **fonte
> única de comando**, o dispositivo, elimina essa classe inteira de conflito. E, tecnicamente, repare:
> é uma SPA sem framework e sem etapa de build — `fetch` e DOM puro num arquivo. Simplicidade é uma
> escolha de arquitetura.

---

## Slide 10 — Segurança: token, segredos e exposição ⏱ 1:00

**No slide**

- Único segredo: o **Auth Token** do device na Blynk
- Fora do código versionado: `secrets.h` (firmware) e `env.js` (app) → ambos no `.gitignore`
- Repositório pode ser **público** sem vazar credencial
- **Limite honesto:** app no browser → token **visível** a quem tem a URL (`?token=`/localStorage)
- **Produção:** backend/proxy guarda o token; o front nunca o vê

**Roteiro de fala**
> Segurança em uma camada. O sistema tem **um** segredo: o Auth Token do dispositivo na nuvem. A
> regra adotada foi: **segredo nunca entra no código versionado**. No firmware ele vive em
> `secrets.h`; no app, em `env.js`. Os dois estão no `.gitignore` — por isso o repositório pode ser
> público sem vazar a credencial.
>
> Mas vou ser honesto sobre o limite do protótipo: como o app roda inteiro no navegador e fala direto
> com a nuvem, o token fica **exposto no browser** — está na URL ou no localStorage, visível a quem
> tiver o link. Para um protótipo de aula, aceitável; **para produção, não**. A correção arquitetural
> é um **backend intermediário** — um proxy — que guarda o token no servidor e nunca o entrega ao
> front-end. Reconhecer o risco e já ter a mitigação desenhada é, em si, boa engenharia.

---

## Slide 11 — Trade-offs técnicos ⏱ 1:00

**No slide**

| Decisão | Escolha | O que se ganha / o que se paga |
|---|---|---|
| Onde decide | **borda (firmware)** | resiliência ↑ · update exige reflash ↓ |
| Config | **JSON em runtime (V10)** | flexibilidade ↑ (o que muda muito sai do firmware) |
| Plataforma | **Blynk gerenciada** (*buy*) | velocidade ↑ · *vendor lock-in* ↓ |
| Hardware | **simulação Wokwi** | custo/iteração ↑ · fidelidade física ↓ |
| Cliente | **SPA sem backend** | simplicidade ↑ · token exposto ↓ |

- Escalabilidade: 1 *template* de pinos → **N devices** sem reescrever firmware nem app

**Roteiro de fala**
> Resumindo as escolhas como trade-offs explícitos. **Inteligência na borda**: ganho resiliência, pago
> com atualização mais cara — mexer na lógica exige reflash. Foi exatamente por isso que movi o que
> **muda com frequência** — os limites — para configuração em runtime no V10. **Blynk em vez de um MQTT
> próprio**: o clássico *build vs. buy* — velocidade de infraestrutura ao custo de *vendor lock-in*.
> **Wokwi em vez de hardware**: itero barato e rápido, sem fidelidade de sensores reais. **SPA sem
> backend**: máxima simplicidade, ao custo do token exposto que vimos.
>
> E o ponto que transforma protótipo em produto: o modelo de **template** de pinos virtuais escala de
> um para **N dispositivos** sem reescrever firmware nem app — cada estufa é só mais um device do mesmo
> template.

---

## Slide 12 — Demonstração & encerramento ⏱ 1:00

**No slide**

- **Demo 1 — device é o cérebro (app fechado):** girar o pot de pH acima de `phHi+phWarn` →
  `[ACT] Bomba Acido -> LIGADA (auto pH alto)`, LED acende, sem browser
- **Demo 2 — fail-safe:** EC alto liga a válvula (diluir); subir o nível a `tankFull` →
  `… DESLIGADA (auto tanque cheio)` mesmo com EC ainda alto
- **Demo 3 — config runtime:** mudar EC máx em *Ajustes* → JSON no V10 → device loga `[CFG]` →
  novo limiar em 1 poll, **sem reflash**
- **Serial Monitor:** linhas `[DATA]` (leituras tagueadas OK/ALERTA/CRITICO) e `[ACT]` (com o motivo)

**Roteiro de fala**
> Para fechar, três demonstrações que provam as decisões técnicas ao vivo. **Uma:** com o app
> **fechado**, eu giro o potenciômetro de pH acima do limite — o LED do ácido acende sozinho e o Serial
> registra `[ACT] … auto pH alto`. A inteligência está mesmo na borda. **Duas:** com a EC alta, a
> válvula abre para diluir; aí eu subo o nível até `tankFull` e ela **fecha**, mesmo com a EC ainda
> alta — o fail-safe tem prioridade. **Três:** mudo o EC máximo na aba Ajustes, salvo, e em um poll o
> comportamento do dispositivo muda — sem recompilar nada.
>
> O resumo técnico: um sistema de controle **distribuído** onde a decisão mora na borda para ter
> resiliência, a configuração viaja em runtime para ter flexibilidade, e cada camada tem um dono único
> de cada dado. Obrigado — perguntas?
