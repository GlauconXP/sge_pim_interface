# Guia inicial para apresentar o SGE

Este roteiro é um ponto de partida. Ajuste os termos e as afirmações ao que o grupo realmente implementou e testou; não diga que uma função está concluída sem conseguir demonstrá-la.

## Objetivo (30–45 segundos)

“O SGE é um protótipo acadêmico para organizar o acompanhamento interno de encomendas em uma unidade de distribuição fictícia. A proposta é reunir cadastro, consulta, atualização de status, histórico de movimentações e gestão de funcionários numa interface única.”

## Demonstração sugerida (3–5 minutos)

1. **Login:** mostrar a autenticação por matrícula e a troca obrigatória da senha temporária no primeiro acesso.
2. **Dashboard:** apontar que os cartões refletem a quantidade de encomendas por status no SQL Server.
3. **Cadastro:** criar uma encomenda de demonstração, escolher um funcionário responsável e mostrar que os dados persistem após atualizar a página.
4. **Consulta:** pesquisar por código, abrir detalhes e mostrar as movimentações registradas.
5. **Atualização de status:** avançar uma única etapa e mostrar a mudança no histórico.
6. **Funcionários e permissões:** mostrar cadastro/edição e comparar, com contas de teste, as opções permitidas para cargos diferentes.
7. **Relatórios:** demonstrar taxa de entrega, pendências, tempo de triagem quando há dados suficientes e distribuição por status.

## Como explicar a arquitetura em linguagem simples

- **HTML** organiza os elementos da página.
- **CSS** controla a aparência e a disposição visual.
- **JavaScript** responde aos cliques, monta as requisições e atualiza a tela.
- **C** recebe as requisições, valida dados e permissões e conversa com o banco.
- **HTTP/JSON** é o formato de comunicação entre a interface e o servidor.
- **ODBC** é a camada usada pelo programa C para se comunicar com o SQL Server.
- **SQL Server** armazena os dados para que continuem disponíveis depois de atualizar a página.
- **Chave estrangeira** conecta uma encomenda ao funcionário responsável.
- **Movimentacoes** registra as etapas para que o sistema possa mostrar o histórico.

## Relação com as disciplinas do PIM

Adapte ao manual oficial do semestre e às exigências do professor:

- **Programação Estruturada em C:** funções, decisões, validações, manipulação de dados e fluxo das requisições no servidor.
- **Modelagem de Banco de Dados:** entidades `Funcionarios`, `Encomendas` e `Movimentacoes`, chaves primárias/estrangeiras e consultas SQL.
- **Redes de Computadores e Sistemas Distribuídos:** comunicação HTTP entre navegador e servidor e troca de mensagens JSON.
- **Engenharia de Software Ágil:** implementação incremental, testes frequentes e organização das alterações em commits.
- **Direitos Humanos:** discussão sobre privacidade, controle de acesso e responsabilidade no tratamento das informações, conforme o escopo definido pelo grupo.

Não apresente a relação com cada disciplina apenas como uma lista: explique um exemplo concreto do sistema para cada uma e confirme que a abordagem corresponde ao conteúdo realmente trabalhado no curso.

## Perguntas que a equipe deve saber responder

1. Qual problema o sistema procura resolver?
2. Por que o banco tem tabelas separadas para funcionários, encomendas e movimentações?
3. O que ocorre quando se cadastra uma encomenda, desde o clique até sua gravação?
4. Qual é a diferença entre atualizar a interface e persistir um dado no banco?
5. Como o sistema impede que qualquer funcionário faça toda operação?
6. Por que a senha não deve ser armazenada em texto puro?
7. Como o grupo testou se um status permaneceu depois de recarregar a página?
8. Quais limitações o protótipo ainda tem e o que seria necessário para uso real?

## Boa prática durante a apresentação

Use dados fictícios. Não mostre senhas, hashes, credenciais de conexão ou informações reais de funcionários. Se perguntarem sobre um detalhe ainda não dominado, explique o que foi implementado e reconheça honestamente o limite do que consegue detalhar.
