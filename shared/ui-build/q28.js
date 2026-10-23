(()=>({edits:(S.edits||[]).map(e=>e.name), presets:(S.presets||[]).filter(p=>p.pre&&p.pre.length||p.post&&p.post.length).map(p=>p.name+':'+((p.pre||[]).length+(p.post||[]).length))}))()
