# Dashboard e relatórios com dados reais

## O que mudou
- Os cartões do Dashboard contam encomendas pelo status atual no SQL Server.
- A API `GET /api/dashboard` disponibiliza os totais do Dashboard a qualquer usuário autenticado.
- A API `GET /api/relatorios` usa a mesma consulta agregada, mas é restrita no servidor a Supervisora/Supervisor/Administrador e Administrativo, conforme as permissões existentes.
- A taxa de entrega é a porcentagem de encomendas cujo status atual é `Entregue` em relação ao total cadastrado. Não é uma taxa histórica de entregas por período.
- O tempo médio de triagem é calculado a partir de `Movimentacoes`, entre os eventos `Em triagem` e o evento seguinte `Separada`. Se não houver pares de eventos no histórico, a interface informa `Sem dados`.
- “Pendentes” significa encomendas que ainda não estão com status `Entregue`. Não é uma contagem de atrasos, porque o esquema atual não tem prazo previsto de entrega.
- A distribuição mostra quantidades e porcentagens para os cinco estados possíveis.

## Compilação e teste
1. Pare o servidor com Ctrl+C.
2. Execute `compilar_sge_corrigido.bat`.
3. Se o resultado for código 0, inicie com `executar_sge.bat`.
4. Faça login e atualize o navegador com Ctrl+F5.
5. Confira os cartões do Dashboard. Entre como supervisora ou administrativo para abrir Relatórios.
6. Cadastre uma encomenda ou avance o status e confira se os números mudam ao retornar ao Dashboard/Relatórios.

As APIs usam a sessão já validada pelo servidor. A API de Relatórios também valida a permissão no backend, não apenas por controle de visibilidade na interface.
