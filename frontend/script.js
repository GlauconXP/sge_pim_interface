let employees = [];

async function carregarFuncionariosDoBanco() {
  const resposta = await fetch("/api/funcionarios");
  if (!resposta.ok) {
    throw new Error("Não foi possível consultar os funcionários no banco de dados.");
  }
  const dados = await resposta.json();
  if (!Array.isArray(dados)) {
    throw new Error("A resposta da lista de funcionários é inválida.");
  }
  employees = dados.map((item) => ({
    id: String(item.matricula ?? ""),
    name: item.nome ?? "Não informado",
    role: item.cargo ?? "",
    sector: item.setor ?? "",
    active: item.ativo === true,
  }));
}
const flow = ["Recebida", "Em triagem", "Separada", "Em distribuição", "Entregue"];
let orders = [];

async function carregarEncomendasDoBanco() {
  const resposta = await fetch("/api/encomendas");

  if (!resposta.ok) {
    throw new Error("Não foi possível consultar as encomendas.");
  }

  const dados = await resposta.json();

  if (!Array.isArray(dados)) {
    throw new Error("A resposta da API é inválida.");
  }

  orders = dados.map((item) => ({
    code: item.codigo ?? "",
    recipient: item.destinatario ?? "Não informado",
    destination: item.cidade ?? "Não informado",
    status: item.status ?? "Recebida",
    owner: String(item.responsavel ?? "").trim() || "Sem responsável",
    date: "—",
    address: "",
    type: "—",
    priority: "Normal",
  }));
}
let indicadoresDashboard = null;

async function buscarIndicadores(endpoint) {
  const resposta = await fetch(endpoint, { cache: "no-store" });
  let dados = {};
  try { dados = await resposta.json(); } catch (_) {}
  if (!resposta.ok) throw new Error(dados.erro || `Não foi possível carregar os indicadores (HTTP ${resposta.status}).`);
  return dados;
}

async function carregarIndicadoresDashboard() {
  try {
    indicadoresDashboard = await buscarIndicadores("/api/dashboard");
    atualizarCartoesDashboard();
  } catch (erro) {
    console.error("Erro ao carregar indicadores do dashboard:", erro);
    indicadoresDashboard = null;
    atualizarCartoesDashboard();
  }
}

function atualizarCartoesDashboard() {
  const dados = indicadoresDashboard;
  document.getElementById("dashRecebidas").textContent = dados ? dados.recebidas : "—";
  document.getElementById("dashEmTriagem").textContent = dados ? dados.emTriagem : "—";
  document.getElementById("dashEmRota").textContent = dados ? (dados.separadas + dados.emDistribuicao) : "—";
  document.getElementById("dashEntregues").textContent = dados ? dados.entregues : "—";
}

function formatarTempoTriagem(minutos) {
  if (minutos === null || minutos === undefined || !Number.isFinite(Number(minutos))) return "Sem dados";
  const totalMinutos = Math.max(0, Math.round(Number(minutos)));
  if (totalMinutos < 60) return `${totalMinutos} min`;
  const horas = Math.floor(totalMinutos / 60);
  const resto = totalMinutos % 60;
  if (horas < 24) return `${horas}h ${resto}min`;
  const dias = Math.floor(horas / 24);
  return `${dias}d ${horas % 24}h`;
}

async function carregarRelatorios() {
  const taxa = document.getElementById("delivery");
  const tempo = document.getElementById("avgTriage");
  const pendentes = document.getElementById("pendingOrders");
  const barras = document.getElementById("statusBars");
  const erroElemento = document.getElementById("reportError");
  taxa.textContent = "…";
  tempo.textContent = "…";
  pendentes.textContent = "…";
  barras.innerHTML = "<p>Consultando dados do banco...</p>";
  erroElemento.textContent = "";

  try {
    const dados = await buscarIndicadores("/api/relatorios");
    taxa.textContent = `${Number(dados.taxaEntrega || 0).toLocaleString("pt-BR", { maximumFractionDigits: 1 })}%`;
    tempo.textContent = formatarTempoTriagem(dados.tempoMedioTriagemMinutos);
    pendentes.textContent = String(dados.pendentes ?? 0);

    const total = Number(dados.total || 0);
    const status = [
      ["Recebida", Number(dados.recebidas || 0)],
      ["Em triagem", Number(dados.emTriagem || 0)],
      ["Separada", Number(dados.separadas || 0)],
      ["Em distribuição", Number(dados.emDistribuicao || 0)],
      ["Entregue", Number(dados.entregues || 0)],
    ];
    barras.innerHTML = status.map(([nome, quantidade]) => {
      const percentual = total > 0 ? Math.round((quantidade / total) * 100) : 0;
      return `<label><span>${escaparHtml(nome)} <small>(${quantidade})</small></span><b>${percentual}%</b><i style="--bar-width:${percentual}%"></i></label>`;
    }).join("");
  } catch (erro) {
    console.error("Erro ao carregar relatórios:", erro);
    taxa.textContent = "—";
    tempo.textContent = "—";
    pendentes.textContent = "—";
    barras.innerHTML = "";
    erroElemento.textContent = erro.message || "Não foi possível carregar os relatórios.";
  }
}

const statusClass = (s) =>
  ({
    Recebida: "b1",
    "Em triagem": "b2",
    Separada: "b3",
    "Em distribuição": "b4",
    Entregue: "b4",
  })[s] || "b1";
const escaparHtml = (valor) =>
  String(valor ?? "").replace(/[&<>"']/g, (caractere) => ({
    "&": "&amp;",
    "<": "&lt;",
    ">": "&gt;",
    '"': "&quot;",
    "'": "&#39;",
  })[caractere]);
const badge = (s) => `<span class="badge ${statusClass(s)}">${escaparHtml(s)}</span>`;
const login = document.getElementById("login"),
  app = document.getElementById("app");
let sessaoAtual = null;

function normalizarCargo(cargo) {
  return String(cargo ?? "")
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "")
    .trim()
    .toLowerCase();
}

function obterPermissoes() {
  const cargo = normalizarCargo(sessaoAtual?.cargo);
  const gerenciaFuncionarios = ["supervisora", "supervisor", "administrador"].includes(cargo);
  return {
    gerenciaFuncionarios,
    cadastraEncomendas: gerenciaFuncionarios || cargo === "atendente",
    avancarPara(status) {
      if (gerenciaFuncionarios) return true;
      if (cargo === "conferente") return ["Em triagem", "Separada"].includes(status);
      if (cargo === "entregador") return ["Em distribuição", "Entregue"].includes(status);
      return false;
    },
    veRelatorios: gerenciaFuncionarios || cargo === "administrativo",
  };
}

function aplicarPermissoes() {
  const permissoes = obterPermissoes();
  const ocultar = (seletor, visivel) => {
    document.querySelectorAll(seletor).forEach((elemento) => {
      elemento.classList.toggle("hidden", !visivel);
    });
  };
  ocultar('[data-page="funcionarios"]', permissoes.gerenciaFuncionarios);
  ocultar('[data-page="cadastro"], [data-go="cadastro"]', permissoes.cadastraEncomendas);
  ocultar('[data-page="relatorios"], [data-go="relatorios"]', permissoes.veRelatorios);
  document.getElementById("newEmployee").classList.toggle("hidden", !permissoes.gerenciaFuncionarios);
  const perfil = document.querySelector(".user small");
  if (perfil) perfil.textContent = sessaoAtual?.cargo || "Funcionário";
  if (!permissoes.gerenciaFuncionarios && document.getElementById("page-funcionarios").classList.contains("active")) {
    navigate("dashboard");
  }
  if (!permissoes.cadastraEncomendas && document.getElementById("page-cadastro").classList.contains("active")) {
    navigate("dashboard");
  }
  if (!permissoes.veRelatorios && document.getElementById("page-relatorios").classList.contains("active")) {
    navigate("dashboard");
  }
}

async function entrarNaAplicacao(sessao) {
  try {
    await Promise.all([carregarEncomendasDoBanco(), carregarFuncionariosDoBanco(), carregarIndicadoresDashboard()]);
    sessaoAtual = sessao;
    aplicarPermissoes();
    login.classList.add("hidden");
    app.classList.remove("hidden");
    document.getElementById("loggedEmployeeName").textContent = sessao.nome || "Funcionário";
    document.getElementById("greetingName").textContent = (sessao.nome || "Funcionário").split(" ")[0];
    renderAll();
  } catch (erro) {
    console.error("Erro ao carregar dados após login:", erro);
    document.getElementById("loginError").textContent = erro.message || "Não foi possível abrir o sistema.";
    await fetch("/api/logout", { method: "POST" }).catch(() => {});
    sessaoAtual = null;
    login.classList.remove("hidden");
    app.classList.add("hidden");
  }
}

function abrirTrocaSenhaObrigatoria(sessao) {
  sessaoAtual = sessao;
  document.getElementById("passwordForm").reset();
  document.getElementById("passwordFormMessage").textContent = "";
  document.getElementById("passwordModal").classList.remove("hidden");
}

async function fazerLogin(matricula, senha) {
  const mensagem = document.getElementById("loginError");
  const botao = document.querySelector('#loginForm button[type="submit"]');
  mensagem.textContent = "Validando credenciais...";
  botao.disabled = true;
  try {
    const resposta = await fetch("/api/login", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ matricula: matricula.trim(), senha }),
    });
    let dados = {};
    try { dados = await resposta.json(); } catch (_) {}
    if (!resposta.ok) throw new Error(dados.erro || "Não foi possível entrar no sistema.");
    mensagem.textContent = "";
    sessaoAtual = dados;
    if (dados.trocarSenha) abrirTrocaSenhaObrigatoria(dados);
    else await entrarNaAplicacao(dados);
  } catch (erro) {
    console.error("Falha de login:", erro);
    mensagem.textContent = erro.message || "Não foi possível entrar no sistema.";
  } finally {
    botao.disabled = false;
  }
}

document.getElementById("loginForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  const matricula = document.getElementById("user").value.trim();
  const senha = document.getElementById("pass").value;
  if (!matricula || !senha) {
    document.getElementById("loginError").textContent = "Preencha matrícula e senha.";
    return;
  }
  await fazerLogin(matricula, senha);
});

document.getElementById("demo").onclick = () => {
  document.getElementById("user").value = "001234";
  document.getElementById("pass").value = "Trocar@SGE2026!";
  document.getElementById("loginError").textContent = "Conta de demonstração preenchida. No primeiro acesso, será necessário trocar a senha.";
};

document.getElementById("passwordForm").addEventListener("submit", async (evento) => {
  evento.preventDefault();
  const mensagem = document.getElementById("passwordFormMessage");
  const atual = document.getElementById("currentPassword").value;
  const nova = document.getElementById("newPassword").value;
  const confirmacao = document.getElementById("confirmPassword").value;
  if (nova.length < 10) {
    mensagem.textContent = "A nova senha deve ter pelo menos 10 caracteres.";
    return;
  }
  if (nova !== confirmacao) {
    mensagem.textContent = "A confirmação não corresponde à nova senha.";
    return;
  }
  const botao = document.getElementById("savePasswordButton");
  botao.disabled = true;
  mensagem.textContent = "Atualizando senha...";
  try {
    const resposta = await fetch("/api/senha", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ senhaAtual: atual, novaSenha: nova }),
    });
    let dados = {};
    try { dados = await resposta.json(); } catch (_) {}
    if (!resposta.ok) throw new Error(dados.erro || "Não foi possível alterar a senha.");
    sessaoAtual.trocarSenha = false;
    document.getElementById("passwordModal").classList.add("hidden");
    document.getElementById("pass").value = "";
    await entrarNaAplicacao(sessaoAtual);
  } catch (erro) {
    mensagem.textContent = erro.message || "Não foi possível alterar a senha.";
  } finally {
    botao.disabled = false;
  }
});

document.getElementById("logout").onclick = async () => {
  try { await fetch("/api/logout", { method: "POST" }); } catch (_) {}
  sessaoAtual = null;
  orders = [];
  employees = [];
  app.classList.add("hidden");
  login.classList.remove("hidden");
  document.getElementById("loginForm").reset();
  document.getElementById("loginError").textContent = "";
};

async function restaurarSessao() {
  try {
    const resposta = await fetch("/api/me", { cache: "no-store" });
    if (!resposta.ok) return;
    const sessao = await resposta.json();
    sessaoAtual = sessao;
    if (sessao.trocarSenha) abrirTrocaSenhaObrigatoria(sessao);
    else await entrarNaAplicacao(sessao);
  } catch (_) {
    // Sem sessão ativa: mantém a tela de login visível.
  }
}
function navigate(p) {
  const permissoes = obterPermissoes();
  if ((p === "funcionarios" && !permissoes.gerenciaFuncionarios) ||
      (p === "cadastro" && !permissoes.cadastraEncomendas) ||
      (p === "relatorios" && !permissoes.veRelatorios)) {
    alert("Seu cargo não tem permissão para acessar essa área.");
    return;
  }
  document
    .querySelectorAll(".page")
    .forEach((x) => x.classList.remove("active"));
  document.getElementById("page-" + p).classList.add("active");
  document
    .querySelectorAll(".nav[data-page]")
    .forEach((x) => x.classList.toggle("active", x.dataset.page === p));
  document.getElementById("title").textContent = {
    dashboard: "Dashboard",
    encomendas: "Encomendas",
    cadastro: "Cadastrar encomenda",
    funcionarios: "Funcionários",
    relatorios: "Relatórios",
  }[p];
  if (p === "relatorios") carregarRelatorios();
  if (p === "dashboard") carregarIndicadoresDashboard();
}
document
  .querySelectorAll(".nav[data-page]")
  .forEach((b) => (b.onclick = () => navigate(b.dataset.page)));
document
  .querySelectorAll("[data-go]")
  .forEach((b) => (b.onclick = () => navigate(b.dataset.go)));
function renderDash() {
  atualizarCartoesDashboard();
  document.getElementById("dashTable").innerHTML = orders
    .slice(0, 5)
    .map(
      (o) =>
        `<tr><td>${escaparHtml(o.code)}</td><td>${escaparHtml(o.recipient)}</td><td>${escaparHtml(o.destination)}</td><td>${badge(o.status)}</td></tr>`,
    )
    .join("");
}
function renderOrders() {
  const q = document.getElementById("search").value.toLowerCase(),
    f = document.getElementById("filter").value;
  const rows = orders.filter(
    (o) =>
      (f === "Todos" || o.status === f) &&
      `${o.code} ${o.recipient} ${o.destination}`.toLowerCase().includes(q),
  );
  const permissoes = obterPermissoes();
  document.getElementById("ordersTable").innerHTML = rows
    .map((o) => {
      const indice = flow.indexOf(o.status);
      const proximoStatus = indice >= 0 ? flow[indice + 1] : null;
      const podeAvancar = proximoStatus && permissoes.avancarPara(proximoStatus);
      const acaoStatus = o.status === "Entregue"
        ? '<button class="link" disabled>Concluída</button>'
        : podeAvancar
          ? `<button class="link advance" data-code="${escaparHtml(o.code)}">Avançar</button>`
          : '<span class="muted-action">Sem permissão para avançar</span>';
      return `<tr><td>${escaparHtml(o.code)}</td><td>${escaparHtml(o.recipient)}</td><td>${escaparHtml(o.destination)}</td><td>${badge(o.status)}</td><td>${escaparHtml(o.owner)}</td><td><button class="link track" data-code="${escaparHtml(o.code)}">Ver</button> · ${acaoStatus}</td></tr>`;
    })
    .join("");
  document
    .querySelectorAll(".track")
    .forEach((b) => (b.onclick = () => openModal(b.dataset.code)));
  document.querySelectorAll(".advance").forEach((botao) => {
    botao.onclick = async () => {
      botao.disabled = true;
      await advance(botao.dataset.code);
      if (botao.isConnected) botao.disabled = false;
    };
  });
}
function renderEmployees() {
  const podeGerenciar = obterPermissoes().gerenciaFuncionarios;
  document.getElementById("employeesTable").innerHTML = employees
    .map((e) => {
      const acoes = podeGerenciar
        ? `<td class="employee-actions"><button class="link employee-edit" data-matricula="${escaparHtml(e.id)}">Editar</button><button class="link employee-toggle" data-matricula="${escaparHtml(e.id)}">${e.active ? "Inativar" : "Ativar"}</button></td>`
        : "<td>Consulta</td>";
      return `<tr><td>${escaparHtml(e.id)}</td><td>${escaparHtml(e.name)}</td><td>${escaparHtml(e.role)}</td><td>${escaparHtml(e.sector)}</td><td>${e.active ? '<span class="badge b4">Ativo</span>' : '<span class="badge b1">Inativo</span>'}</td>${acoes}</tr>`;
    })
    .join("");

  document.querySelectorAll(".employee-edit").forEach((botao) => {
    botao.onclick = () => abrirModalFuncionario(botao.dataset.matricula);
  });
  document.querySelectorAll(".employee-toggle").forEach((botao) => {
    botao.onclick = () => alternarSituacaoFuncionario(botao.dataset.matricula);
  });

  const seletor = document.getElementById("owner");
  const valorAnterior = seletor.value;
  seletor.innerHTML =
    '<option value="">Sem responsável</option>' +
    employees
      .filter((e) => e.active)
      .map(
        (e) =>
          `<option value="${escaparHtml(e.id)}">${escaparHtml(e.name)} — ${escaparHtml(e.sector)}</option>`,
      )
      .join("");
  if ([...seletor.options].some((opcao) => opcao.value === valorAnterior)) {
    seletor.value = valorAnterior;
  }
}
function renderAll() {
  renderDash();
  renderOrders();
  renderEmployees();
}
document.getElementById("search").oninput = renderOrders;
document.getElementById("filter").onchange = renderOrders;
async function advance(code) {
  const encomenda = orders.find((item) => item.code === code);
  if (!encomenda) {
    alert("Não encontrei essa encomenda na lista.");
    return;
  }

  const indice = flow.indexOf(encomenda.status);
  if (indice < 0) {
    alert("O status atual dessa encomenda não é reconhecido.");
    return;
  }
  if (indice >= flow.length - 1) {
    alert("Essa encomenda já está marcada como entregue.");
    return;
  }

  const proximoStatus = flow[indice + 1];

  try {
    const resposta = await fetch("/api/encomendas/status", {
      method: "PUT",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ code: encomenda.code, status: proximoStatus }),
    });

    let resultado = {};
    try {
      resultado = await resposta.json();
    } catch (_) {
      // Mantém uma mensagem genérica caso o servidor não retorne JSON.
    }

    if (!resposta.ok) {
      throw new Error(
        resultado.erro || `Não foi possível atualizar o status (HTTP ${resposta.status}).`,
      );
    }

    // Só atualiza a tela depois que o SQL Server confirmar a alteração.
    await carregarEncomendasDoBanco();
    await carregarIndicadoresDashboard();
    renderAll();
  } catch (erro) {
    console.error("Erro ao atualizar status:", erro);
    alert(erro.message || "Não foi possível atualizar o status da encomenda.");
  }
}
document.getElementById("packageForm").addEventListener("submit", async (e) => {
  e.preventDefault();

  const form = e.currentTarget;
  const dados = new FormData(form);
  const codigo = String(dados.get("code") ?? "").trim().toUpperCase();
  const mensagem = document.getElementById("success");
  mensagem.textContent = "";

  if (orders.some((encomenda) => encomenda.code.toUpperCase() === codigo)) {
    mensagem.textContent = "Já existe uma encomenda com esse código.";
    return;
  }

  const encomenda = {
    code: codigo,
    recipient: String(dados.get("recipient") ?? "").trim(),
    address: String(dados.get("address") ?? "").trim(),
    cep: String(dados.get("cep") ?? "").trim(),
    neighborhood: String(dados.get("neighborhood") ?? "").trim(),
    city: String(dados.get("city") ?? "").trim(),
    type: String(dados.get("type") ?? "SEDEX"),
    status: String(dados.get("status") ?? "Recebida"),
    priority: String(dados.get("priority") ?? "Normal"),
    ownerMatricula: String(dados.get("owner") ?? "").trim(),
  };

  try {
    const resposta = await fetch("/api/encomendas", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(encomenda),
    });

    let resultado = {};
    try {
      resultado = await resposta.json();
    } catch (_) {
      // A API pode retornar uma resposta sem JSON em caso de erro.
    }

    if (!resposta.ok) {
      throw new Error(
        resultado.erro || `Não foi possível cadastrar (HTTP ${resposta.status}).`,
      );
    }

    // Recarrega a lista com os dados confirmados no SQL Server.
    await carregarEncomendasDoBanco();
    await carregarIndicadoresDashboard();
    renderAll();
    form.reset();
    mensagem.textContent = "Encomenda cadastrada com sucesso no banco de dados.";
  } catch (erro) {
    console.error("Erro ao cadastrar encomenda:", erro);
    mensagem.textContent =
      erro.message || "Erro ao cadastrar. Confira se o servidor C está funcionando.";
  }
});
async function openModal(code) {
  const encomenda = orders.find((item) => item.code === code);
  if (!encomenda) {
    alert("Não encontrei essa encomenda na lista.");
    return;
  }

  document.getElementById("modalCode").textContent = encomenda.code;
  document.getElementById("modalBody").innerHTML =
    `<div class="tracking"><div class="box"><b>Resumo</b><p><b>Destinatário:</b> ${escaparHtml(encomenda.recipient)}</p><p><b>Destino:</b> ${escaparHtml(encomenda.destination)}</p><p><b>Status atual:</b> ${badge(encomenda.status)}</p><p><b>Responsável:</b> ${escaparHtml(encomenda.owner)}</p><p><b>Tipo:</b> ${escaparHtml(encomenda.type)}</p><p><b>Prioridade:</b> ${escaparHtml(encomenda.priority)}</p></div><div class="box"><b>Histórico de movimentações</b><div id="historyContent"><p>Consultando histórico...</p></div></div></div>`;
  document.getElementById("modal").classList.remove("hidden");

  const conteudoHistorico = document.getElementById("historyContent");
  try {
    const resposta = await fetch(
      `/api/encomendas/historico?codigo=${encodeURIComponent(encomenda.code)}`,
    );
    const dados = await resposta.json();

    if (!resposta.ok) {
      throw new Error(dados.erro || "Não foi possível consultar o histórico.");
    }
    if (!Array.isArray(dados)) {
      throw new Error("A resposta do histórico é inválida.");
    }

    if (dados.length === 0) {
      conteudoHistorico.innerHTML =
        '<p>Nenhuma movimentação foi registrada para esta encomenda ainda.</p>';
      return;
    }

    conteudoHistorico.innerHTML = `<div class="timeline">${dados.map((movimentacao, indice) =>
      `<div class="${indice === dados.length - 1 ? "current" : "done"}"><span class="dot"></span><p><b>${escaparHtml(movimentacao.status)}</b><br><small>${escaparHtml(movimentacao.dataHora)}</small>${movimentacao.observacao ? `<br><small>${escaparHtml(movimentacao.observacao)}</small>` : ""}</p></div>`,
    ).join("")}</div>`;
  } catch (erro) {
    console.error("Erro ao carregar histórico:", erro);
    conteudoHistorico.innerHTML = `<p class="error">${escaparHtml(erro.message || "Erro ao consultar o histórico.")}</p>`;
  }
}

document.getElementById("quickTrack").onclick = () => {
  if (orders.length === 0) {
    alert("Não há encomendas cadastradas para consultar.");
    return;
  }
  openModal(orders[0].code);
};
document.getElementById("closeModal").onclick = () =>
  document.getElementById("modal").classList.add("hidden");
document.querySelector("#modal .backdrop").onclick = () =>
  document.getElementById("modal").classList.add("hidden");
document.getElementById("refresh").onclick = () => carregarRelatorios();
let matriculaOriginalEmEdicao = null;

function abrirModalFuncionario(matricula = null) {
  const funcionario = matricula === null
    ? null
    : employees.find((item) => item.id === matricula);
  if (matricula !== null && !funcionario) {
    alert("Não encontrei esse funcionário. Atualize a lista e tente novamente.");
    return;
  }

  const form = document.getElementById("employeeForm");
  form.reset();
  matriculaOriginalEmEdicao = funcionario ? funcionario.id : null;
  document.getElementById("employeeModalTitle").textContent =
    funcionario ? "Editar funcionário" : "Novo funcionário";
  document.getElementById("saveEmployeeButton").textContent =
    funcionario ? "Salvar alterações" : "Cadastrar funcionário";
  document.getElementById("employeeFormMessage").textContent = "";
  form.elements.namedItem("matricula").value = funcionario ? funcionario.id : "";
  form.elements.namedItem("nome").value = funcionario ? funcionario.name : "";
  form.elements.namedItem("cargo").value = funcionario ? funcionario.role : "";
  form.elements.namedItem("setor").value = funcionario ? funcionario.sector : "";
  form.elements.namedItem("ativo").value = funcionario ? String(funcionario.active) : "true";
  form.elements.namedItem("senha").value = "";
  form.elements.namedItem("senha").required = !funcionario;
  document.getElementById("employeePasswordHelp").textContent = funcionario
    ? "Opcional ao editar. Se preenchida, a senha será redefinida e deverá ser trocada no próximo login."
    : "Obrigatória ao cadastrar, com pelo menos 10 caracteres. Será trocada no primeiro login.";
  document.getElementById("employeeModal").classList.remove("hidden");
}

function fecharModalFuncionario() {
  document.getElementById("employeeModal").classList.add("hidden");
  document.getElementById("employeeFormMessage").textContent = "";
}

document.getElementById("newEmployee").onclick = () => abrirModalFuncionario();
document.getElementById("closeEmployeeModal").onclick = fecharModalFuncionario;
document.getElementById("cancelEmployeeModal").onclick = fecharModalFuncionario;
document.querySelector(".employee-backdrop").onclick = fecharModalFuncionario;

document.getElementById("employeeForm").addEventListener("submit", async (evento) => {
  evento.preventDefault();
  const form = evento.currentTarget;
  const dados = new FormData(form);
  const mensagem = document.getElementById("employeeFormMessage");
  const botaoSalvar = document.getElementById("saveEmployeeButton");
  const editando = matriculaOriginalEmEdicao !== null;
  const payload = {
    originalMatricula: matriculaOriginalEmEdicao,
    matricula: String(dados.get("matricula") ?? "").trim(),
    nome: String(dados.get("nome") ?? "").trim(),
    cargo: String(dados.get("cargo") ?? "").trim(),
    setor: String(dados.get("setor") ?? "").trim(),
    ativo: String(dados.get("ativo")) === "true",
    senha: String(dados.get("senha") ?? ""),
  };

  if (!payload.matricula || !payload.nome || !payload.cargo || !payload.setor) {
    mensagem.textContent = "Preencha todos os campos antes de salvar.";
    return;
  }
  if ((!editando || payload.senha) && payload.senha.length < 10) {
    mensagem.textContent = "A senha deve ter pelo menos 10 caracteres.";
    return;
  }

  botaoSalvar.disabled = true;
  mensagem.textContent = "Salvando...";
  try {
    await enviarFuncionario(payload, editando ? "PUT" : "POST");
    await Promise.all([carregarFuncionariosDoBanco(), carregarEncomendasDoBanco()]);
    renderAll();
    fecharModalFuncionario();
    document.getElementById("employeeNotice").textContent = editando
      ? "Funcionário atualizado com sucesso."
      : "Funcionário cadastrado com sucesso.";
  } catch (erro) {
    console.error("Erro ao salvar funcionário:", erro);
    mensagem.textContent = erro.message || "Não foi possível salvar o funcionário.";
  } finally {
    botaoSalvar.disabled = false;
  }
});

async function enviarFuncionario(payload, metodo) {
  const resposta = await fetch("/api/funcionarios", {
    method: metodo,
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });
  let resultado = {};
  try {
    resultado = await resposta.json();
  } catch (_) {
    // Mantém mensagem padrão se a API não enviar JSON.
  }
  if (!resposta.ok) {
    throw new Error(resultado.erro || `Não foi possível salvar o funcionário (HTTP ${resposta.status}).`);
  }
  return resultado;
}

async function alternarSituacaoFuncionario(matricula) {
  const funcionario = employees.find((item) => item.id === matricula);
  if (!funcionario) return;
  const novaSituacao = !funcionario.active;
  const acao = novaSituacao ? "ativar" : "inativar";
  if (!confirm(`Deseja ${acao} o funcionário ${funcionario.name}?`)) return;

  try {
    await enviarFuncionario({
      originalMatricula: funcionario.id,
      matricula: funcionario.id,
      nome: funcionario.name,
      cargo: funcionario.role,
      setor: funcionario.sector,
      ativo: novaSituacao,
    }, "PUT");
    await Promise.all([carregarFuncionariosDoBanco(), carregarEncomendasDoBanco()]);
    renderAll();
    document.getElementById("employeeNotice").textContent =
      `Funcionário ${novaSituacao ? "ativado" : "inativado"} com sucesso.`;
  } catch (erro) {
    console.error("Erro ao alterar situação do funcionário:", erro);
    alert(erro.message || "Não foi possível alterar a situação do funcionário.");
  }
}

navigate("dashboard");
restaurarSessao();
