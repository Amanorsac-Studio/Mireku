-- =====================================================================
--  MIREKU: "tell me when it's out" sign-ups from jamesmireku.com
--
--  Run once in Supabase -> SQL Editor. Safe to run again.
--
--  Who can do what:
--    visitors (anon key)      ADD a row, nothing else. They cannot read,
--                             change or delete anyone's entry, their own
--                             included, so the list cannot be scraped
--                             through the website's public key.
--    studio admins            READ, through public.is_admin().
--    you, in the dashboard    everything: Table Editor -> mireku_interest,
--                             and Export to CSV.
--
--  The checks below are the real rules; the form only mirrors them.
-- =====================================================================

create table if not exists public.mireku_interest (
  id          bigserial primary key,
  created_at  timestamptz not null default now(),
  name        text not null check (char_length(btrim(name)) between 1 and 120),
  phone       text not null check (phone ~ '^\+?[0-9][0-9 ()-]{5,22}$'),
  email       text check (email is null or (char_length(email) <= 200 and email ~ '^[^@\s]+@[^@\s]+\.[^@\s]+$')),
  consent     boolean not null check (consent),
  source      text not null default 'jamesmireku.com' check (char_length(source) <= 60)
);

-- One entry per number, however it was typed (spaces, brackets, dashes).
create unique index if not exists mireku_interest_phone_once
  on public.mireku_interest ((regexp_replace(phone, '[^0-9]', '', 'g')));

alter table public.mireku_interest enable row level security;

drop policy if exists mireku_interest_add on public.mireku_interest;
create policy mireku_interest_add on public.mireku_interest
  for insert to anon, authenticated
  with check (consent and created_at > now() - interval '1 minute');

drop policy if exists mireku_interest_admin_read on public.mireku_interest;
create policy mireku_interest_admin_read on public.mireku_interest
  for select to authenticated
  using ((select public.is_admin()));

revoke all on public.mireku_interest from anon, authenticated;
grant insert (name, phone, email, consent, source) on public.mireku_interest to anon, authenticated;
grant select on public.mireku_interest to authenticated;
grant usage on sequence public.mireku_interest_id_seq to anon, authenticated;

select 'mireku_interest is ready' as done;
