ext_mem : entity work.axi_mem
  port map (
    clk => clk,
    nreset => nreset,
    -- AXI slave port
    s_axi_awid     => "1",
    s_axi_awaddr   => m_axi_awaddr(15 downto 0),
    s_axi_awlen    => (others => '0'),
    s_axi_awsize   => "010",
    s_axi_awburst  => "00",
    s_axi_awvalid  => m_axi_awvalid,
    s_axi_awready  => m_axi_awready,
    s_axi_wdata    => m_axi_wdata,
    s_axi_wstrb    => m_axi_wstrb,
    s_axi_wvalid   => m_axi_wvalid,
    s_axi_wready   => m_axi_wready,
    s_axi_bid      => open,
    s_axi_bresp    => open,
    s_axi_bvalid   => m_axi_bvalid,
    s_axi_bready   => m_axi_bready,
    s_axi_arid     => "1",
    s_axi_araddr   => m_axi_araddr(15 downto 0),
    s_axi_arlen    => (others => '0'),
    s_axi_arsize   => "010",
    s_axi_arburst  => "00",
    s_axi_arvalid  => m_axi_arvalid,
    s_axi_arready  => m_axi_arready,
    s_axi_rid      => open,
    s_axi_rdata    => m_axi_rdata,
    s_axi_rresp    => open,
    s_axi_rlast    => open,
    s_axi_rvalid   => m_axi_rvalid,
    s_axi_rready   => m_axi_rready
  );