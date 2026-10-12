-- Confira o status atual e o histórico registrado para cada encomenda.
-- Edite o WHERE para filtrar um código específico, se necessário.
SELECT
    e.Codigo,
    e.Status AS StatusAtual,
    m.Status AS StatusRegistrado,
    m.DataHora,
    m.Observacao
FROM dbo.Encomendas AS e
LEFT JOIN dbo.Movimentacoes AS m
    ON m.EncomendaId = e.Id
ORDER BY e.Codigo, m.DataHora, m.Id;
